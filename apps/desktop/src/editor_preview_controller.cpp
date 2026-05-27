#include "editor_preview_controller.h"

#include "ai_model_runtime.h"
#include "app_bootstrap.h"
#include "brush_preview_compositor.h"
#include "decoded_image_cache.h"
#include "editor_preview_image_sink.h"
#include "editor_preview_texture.h"
#include "observability.h"
#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/lens_profile.h"
#include "raw_image_decoder.h"
#include "vision_segmentation.h"
#include "working_image.h"

#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#endif

#include <QtMath>
#include <QtConcurrent>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <QCryptographicHash>
#include <QColorSpace>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QBuffer>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QColor>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QProcess>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QTransform>
#include <QUrl>
#include <QUrlQuery>
#include <limits>
#include <queue>
#include <vector>

namespace photo_editor::desktop {

struct RenderBackendTelemetry;

[[nodiscard]] QImage render_output_image(
    const QImage& image,
    const EditRecipeState& state,
    const QColorSpace& target_color_space,
    QImage::Format output_format,
    const photo_editor::pipeline::DecodedImageMetadata* metadata,
    RenderBackendTelemetry* telemetry = nullptr);

namespace {

constexpr int kDefaultInteractivePreviewEdge = 960;
constexpr int kMinimumInteractivePreviewEdge = 512;
constexpr int kMaximumInteractivePreviewEdge = 4096;
constexpr int kExpensiveInteractivePreviewEdge = 2048;
constexpr int kDraftInteractivePreviewEdge = 1024;
constexpr int kExpensiveDraftInteractivePreviewEdge = 384;
constexpr int kHistogramPreviewEdge = 1024;
constexpr int kFullRenderDebounceMs = 700;
constexpr int kExpensiveInteractiveRenderDebounceMs = 45;
constexpr int kExpensiveFullRenderDebounceMs = 1800;
constexpr int kSmartSelectPreviewDebounceMs = 45;
constexpr qint64 kInteractiveDraftBurstWindowMs = 180;
constexpr qint64 kMaxLinearCacheMemoryBytes = 256 * 1024 * 1024;  // 256 MB
constexpr int kMaxEditHistoryDepth = 100;

constexpr auto kEditedPreviewImageId = "edited";
constexpr auto kReferencePreviewImageId = "reference";
constexpr auto kEditedPreviewOverlayImageId = "edited-overlay";
constexpr auto kReferencePreviewOverlayImageId = "reference-overlay";
constexpr auto kActiveBrushLivePreviewImageId = "brush-live";

QString normalized_render_reason(const QString& reason, bool schedule_full_resolution) {
  const auto normalized = reason.trimmed().toLower();
  if (!normalized.isEmpty()) {
    return normalized;
  }
  return schedule_full_resolution ? QStringLiteral("edit_change") : QStringLiteral("interactive_change");
}

bool render_reason_invalidates_preview_overlay(const QString& reason) {
  return reason == QStringLiteral("asset_select") ||
         reason == QStringLiteral("compare_toggle") ||
         reason == QStringLiteral("compare_adjustment_change") ||
         reason == QStringLiteral("reference_change") ||
         reason == QStringLiteral("crop_change") ||
         reason == QStringLiteral("output_profile_change");
}

bool force_draft_interactive_for_reason(const QString& reason) {
  return reason == QStringLiteral("mask_preview_change");
}

QString interaction_mode_for_request(bool full_resolution, bool draft_interactive) {
  if (full_resolution) {
    return QStringLiteral("full_resolution");
  }
  return draft_interactive ? QStringLiteral("draft_interactive") : QStringLiteral("interactive");
}

struct BrushPreset final {
  QString name;
  double size = 0.08;
  double flow = 1.0;
  double hardness = 0.25;
  double spacing = 0.75;
  double smoothing = 0.0;
  double feather = 0.75;
  bool buildup = true;
  bool auto_mask = false;
  double auto_mask_tolerance = 0.18;
};

const std::array<BrushPreset, 7>& brush_presets() {
  static const std::array<BrushPreset, 7> presets = {{
      {QStringLiteral("Soft Round"), 0.08, 1.0, 0.25, 0.75, 0.0, 0.75, true, false, 0.18},
      {QStringLiteral("Selection Brush"), 0.06, 1.0, 0.45, 0.75, 0.18, 0.55, true, false, 0.18},
      {QStringLiteral("Detail"), 0.035, 0.82, 0.58, 0.45, 0.24, 0.45, true, false, 0.18},
      {QStringLiteral("Edge Aware"), 0.026, 1.0, 0.66, 0.42, 0.34, 0.34, true, true, 0.14},
      {QStringLiteral("Retouch Soft"), 0.08, 0.92, 0.14, 0.55, 0.12, 0.86, true, false, 0.18},
      {QStringLiteral("Heal Detail"), 0.07, 0.78, 0.20, 0.45, 0.22, 0.80, true, true, 0.16},
      {QStringLiteral("Clone Stamp"), 0.07, 0.90, 0.26, 0.75, 0.08, 0.74, true, false, 0.18},
  }};
  return presets;
}

const BrushPreset* brush_preset_by_name(const QString& name) {
  const auto normalized_name = name.simplified();
  for (const auto& preset : brush_presets()) {
    if (preset.name.compare(normalized_name, Qt::CaseInsensitive) == 0) {
      return &preset;
    }
  }
  return nullptr;
}

void apply_brush_preset_to_mask(QVariantMap* mask, const BrushPreset& preset) {
  if (mask == nullptr) {
    return;
  }
  mask->insert(QStringLiteral("brushPreset"), preset.name);
  mask->insert(QStringLiteral("brushSize"), preset.size);
  mask->insert(QStringLiteral("brushFlow"), preset.flow);
  mask->insert(QStringLiteral("brushHardness"), preset.hardness);
  mask->insert(QStringLiteral("brushSpacing"), preset.spacing);
  mask->insert(QStringLiteral("brushSmoothing"), preset.smoothing);
  mask->insert(QStringLiteral("brushBuildup"), preset.buildup);
  mask->insert(QStringLiteral("brushEraseMode"), false);
  mask->insert(QStringLiteral("brushAutoMask"), preset.auto_mask);
  mask->insert(QStringLiteral("brushAutoMaskTolerance"), preset.auto_mask_tolerance);
  mask->insert(QStringLiteral("feather"), preset.feather);
}

using LinearImageBuffer = WorkingImage;

struct LinearImageCache final {
  QMutex mutex;
  QHash<QString, std::shared_ptr<LinearImageBuffer>> images;
  QList<QString> access_order;  // front = most recently used
  qint64 total_bytes = 0;

  void touch(const QString& key) {
    access_order.removeOne(key);
    access_order.prepend(key);
  }

  void insert(const QString& key, const std::shared_ptr<LinearImageBuffer>& image) {
    if (!image) {
      return;
    }
    if (images.contains(key)) {
      total_bytes -= static_cast<qint64>(images.value(key)->pixels.size() * sizeof(photo_editor::pipeline::ColorPixel));
      images.remove(key);
      access_order.removeOne(key);
    }
    const auto image_bytes =
        static_cast<qint64>(image->pixels.size() * sizeof(photo_editor::pipeline::ColorPixel));
    while (total_bytes + image_bytes > kMaxLinearCacheMemoryBytes && !access_order.isEmpty()) {
      const auto& evict_key = access_order.last();
      total_bytes -= static_cast<qint64>(
          images.value(evict_key)->pixels.size() * sizeof(photo_editor::pipeline::ColorPixel));
      images.remove(evict_key);
      access_order.removeLast();
    }
    images.insert(key, image);
    access_order.prepend(key);
    total_bytes += image_bytes;
  }
};

using CacheSnapshot = DecodedImageCacheSnapshot;

struct DecodeSourceTelemetry final {
  bool cache_lookup = false;
  bool cache_hit = false;
  QString backend;
  CacheSnapshot decoded_cache;
};

struct PreviewAdjustmentTelemetry final {
  QString backend;
  QString fallback_reason;
  bool linear_cache_lookup = false;
  bool linear_cache_hit = false;
  CacheSnapshot linear_cache;
};

LinearImageCache& linear_image_cache() {
  static LinearImageCache cache;
  return cache;
}

CacheSnapshot linear_image_cache_snapshot() {
  auto& cache = linear_image_cache();
  QMutexLocker locker(&cache.mutex);
  return CacheSnapshot{
      .entries = static_cast<int>(cache.images.size()),
      .bytes = cache.total_bytes,
  };
}

RawDecodeSettings raw_decode_settings_for_state(const EditState& state) {
  return RawDecodeSettings{
      .input_transform_enabled = state.raw_input_transform_enabled,
      .input_color_profile = state.input_color_profile,
      .highlight_reconstruction_mode = state.highlight_reconstruction_mode,
      .chromatic_adaptation_method = state.chromatic_adaptation_method,
      .demosaic_quality = state.raw_demosaic_quality,
      .exposure = state.raw_exposure,
      .boost_amount = state.raw_boost,
      .boost_shadow_amount = state.raw_shadow_boost,
      .detail_amount = state.raw_detail,
      .chroma_noise_reduction_amount = state.raw_chroma_noise_reduction,
      .moire_reduction_amount = state.raw_moire_reduction,
      .hot_dead_pixel_cleanup_amount = state.raw_hot_dead_pixel_cleanup,
      .false_color_suppression_amount = state.raw_false_color_suppression,
      .highlight_recovery_enabled = state.raw_highlight_recovery,
      .gamut_mapping_enabled = state.raw_gamut_mapping,
      .lens_correction_enabled = state.raw_lens_correction,
      .white_balance_override_enabled = state.raw_white_balance_override,
      .white_balance_temperature = state.raw_white_balance_temperature,
      .white_balance_tint = state.raw_white_balance_tint,
  };
}

QImage normalize_preview_color_space(QImage image);
QColorSpace preview_target_color_space() {
  return QColorSpace(QColorSpace::SRgb);
}

QColorSpace output_color_space_for_name(const QString& name) {
  return preview_color_space_for_name(normalized_output_color_space_name(name));
}

void apply_output_color_space_transform(QImage* image, const QString& output_color_space_name) {
  if (image == nullptr || image->isNull()) {
    return;
  }

  const auto target_color_space = output_color_space_for_name(output_color_space_name);
  if (!target_color_space.isValid()) {
    return;
  }

  auto working_image = image->colorSpace().isValid() ? *image : normalize_preview_color_space(*image);
  const auto source_color_space = working_image.colorSpace().isValid()
                                      ? working_image.colorSpace()
                                      : preview_target_color_space();
  if (source_color_space == target_color_space) {
    working_image.setColorSpace(target_color_space);
    *image = std::move(working_image);
    return;
  }

  const auto converted = working_image.convertedToColorSpace(target_color_space);
  if (!converted.isNull()) {
    *image = converted;
    return;
  }

  working_image.setColorSpace(source_color_space);
  *image = std::move(working_image);
}

QImage normalize_preview_color_space(QImage image) {
  if (image.isNull()) {
    return image;
  }

  const auto target_color_space = preview_target_color_space();
  const auto source_color_space = image.colorSpace();
  if (source_color_space.isValid() && source_color_space != target_color_space) {
    const auto converted = image.convertedToColorSpace(target_color_space);
    if (!converted.isNull()) {
      image = converted;
    }
  }

  if (image.format() != QImage::Format_ARGB32) {
    image = image.convertToFormat(QImage::Format_ARGB32);
  }
  image.setColorSpace(target_color_space);
  return image;
}

QString preview_output_color_space_for_state(const EditState& state) {
  return state.preview_color_space.trimmed().isEmpty() ? state.output_color_space : state.preview_color_space;
}

QString monitor_display_color_profile_for_state(const EditState& state) {
  const auto monitor_profile = normalized_monitor_color_profile_name(state.monitor_color_profile);
  return monitor_profile.isEmpty() ? preview_output_color_space_for_state(state) : monitor_profile;
}

void apply_preview_display_transform(QImage* image, const EditState& state) {
  if (image == nullptr || image->isNull()) {
    return;
  }

  if (state.soft_proof_enabled) {
    apply_output_color_space_transform(image, state.soft_proof_profile);
  }
  apply_output_color_space_transform(image, monitor_display_color_profile_for_state(state));
}

photo_editor::pipeline::ColorPipelineConfig color_pipeline_config_for_state(const EditState& state) {
  return photo_editor::pipeline::ColorPipelineConfig{
      .input_profile =
          photo_editor::pipeline::input_color_profile_from_name(state.input_color_profile.toStdString()),
      .working_profile =
          photo_editor::pipeline::working_color_profile_from_name(state.working_color_profile.toStdString()),
      .preview_output_profile = photo_editor::pipeline::output_color_profile_from_name(
          preview_output_color_space_for_state(state).toStdString()),
      .export_output_profile =
          photo_editor::pipeline::output_color_profile_from_name(state.output_color_space.toStdString()),
      .camera_profile = {},
      .input_transform_recipe = {},
      .render_intent =
          photo_editor::pipeline::render_intent_from_name(state.render_intent.toStdString()),
      .output_headroom = state.output_headroom,
  };
}

photo_editor::pipeline::ColorPipelineConfig color_pipeline_config_for_state(
    const EditState& state,
    const photo_editor::pipeline::DecodedImageMetadata* metadata) {
  auto config = color_pipeline_config_for_state(state);
  if (metadata != nullptr && metadata->valid) {
    config.camera_profile = photo_editor::pipeline::resolve_camera_profile(
        *metadata,
        config.input_profile);
    config.input_transform_recipe = photo_editor::pipeline::resolve_input_transform_recipe(
        config.input_profile,
        config.camera_profile,
        *metadata);
  }
  return config;
}

struct RawClippingStats final {
  int sample_count = 0;
  int shadow_clip_count = 0;
  int highlight_clip_count = 0;
};

RawClippingStats raw_clipping_stats_for_image(const QImage& image) {
  RawClippingStats stats;
  if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
    return stats;
  }

  constexpr int kRawClippingSampleTarget = 120000;
  constexpr double kClipEpsilon = 1.0 / 65535.0;
  const auto total_pixels = std::max(1, image.width() * image.height());
  const auto sample_stride = std::max(
      1,
      static_cast<int>(
          std::sqrt(static_cast<double>(total_pixels) / static_cast<double>(kRawClippingSampleTarget))));
  for (int y = 0; y < image.height(); y += sample_stride) {
    for (int x = 0; x < image.width(); x += sample_stride) {
      const auto color = image.pixelColor(x, y);
      const auto red = color.redF();
      const auto green = color.greenF();
      const auto blue = color.blueF();
      ++stats.sample_count;
      if (red <= kClipEpsilon || green <= kClipEpsilon || blue <= kClipEpsilon) {
        ++stats.shadow_clip_count;
      }
      if (red >= 1.0 - kClipEpsilon || green >= 1.0 - kClipEpsilon ||
          blue >= 1.0 - kClipEpsilon) {
        ++stats.highlight_clip_count;
      }
    }
  }
  return stats;
}

double display_luminance_for_color(const QColor& color) {
  return color.redF() * 0.2126 + color.greenF() * 0.7152 + color.blueF() * 0.0722;
}

QVariantMap pixel_sample_map(const QImage& image, double normalized_x, double normalized_y) {
  QVariantMap sample;
  if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
    sample.insert(QStringLiteral("valid"), false);
    return sample;
  }

  const auto x = std::clamp(
      static_cast<int>(std::lround(normalized_x * static_cast<double>(std::max(image.width() - 1, 0)))),
      0,
      std::max(image.width() - 1, 0));
  const auto y = std::clamp(
      static_cast<int>(std::lround(normalized_y * static_cast<double>(std::max(image.height() - 1, 0)))),
      0,
      std::max(image.height() - 1, 0));
  const auto color = image.pixelColor(x, y);

  sample.insert(QStringLiteral("valid"), true);
  sample.insert(QStringLiteral("pixelX"), x);
  sample.insert(QStringLiteral("pixelY"), y);
  sample.insert(QStringLiteral("red"), color.red());
  sample.insert(QStringLiteral("green"), color.green());
  sample.insert(QStringLiteral("blue"), color.blue());
  sample.insert(QStringLiteral("alpha"), color.alpha());
  sample.insert(QStringLiteral("redF"), color.redF());
  sample.insert(QStringLiteral("greenF"), color.greenF());
  sample.insert(QStringLiteral("blueF"), color.blueF());
  sample.insert(QStringLiteral("alphaF"), color.alphaF());
  sample.insert(QStringLiteral("hex"), color.name(QColor::HexRgb).toUpper());
  sample.insert(QStringLiteral("luminance"), display_luminance_for_color(color));
  return sample;
}

QString raw_decode_settings_key(const EditState& state) {
  return QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11|%12|%13|%14|%15|%16|%17|%18|%19")
      .arg(state.raw_input_transform_enabled ? 1 : 0)
      .arg(normalized_input_color_profile_name(state.input_color_profile))
      .arg(normalized_highlight_reconstruction_mode_name(state.highlight_reconstruction_mode))
      .arg(normalized_chromatic_adaptation_method_name(state.chromatic_adaptation_method))
      .arg(normalized_raw_demosaic_quality_name(state.raw_demosaic_quality))
      .arg(state.raw_exposure, 0, 'f', 6)
      .arg(state.raw_boost, 0, 'f', 6)
      .arg(state.raw_shadow_boost, 0, 'f', 6)
      .arg(state.raw_detail, 0, 'f', 6)
      .arg(state.raw_chroma_noise_reduction, 0, 'f', 6)
      .arg(state.raw_moire_reduction, 0, 'f', 6)
      .arg(state.raw_hot_dead_pixel_cleanup, 0, 'f', 6)
      .arg(state.raw_false_color_suppression, 0, 'f', 6)
      .arg(state.raw_highlight_recovery ? 1 : 0)
      .arg(state.raw_gamut_mapping ? 1 : 0)
      .arg(state.raw_lens_correction ? 1 : 0)
      .arg(state.raw_white_balance_override ? 1 : 0)
      .arg(state.raw_white_balance_temperature, 0, 'f', 3)
      .arg(state.raw_white_balance_tint, 0, 'f', 3);
}

QString preview_cache_root() {
  const auto cache_root = AppBootstrap::cacheLocation();
  const auto base = cache_root.isEmpty() ? QDir::current().absoluteFilePath(QStringLiteral(".photo-editor-cache")) : cache_root;
  return QDir(base).filePath(QStringLiteral("previews"));
}

QString versioned_file_url(const QString& file_path) {
  const QFileInfo file_info(file_path);
  auto url = QUrl::fromLocalFile(file_info.absoluteFilePath());
  return url.toString();
}

QString local_file_path_from_user_input(const QString& path_or_url) {
  const auto trimmed = path_or_url.trimmed();
  if (trimmed.isEmpty()) {
    return {};
  }
  const QUrl url(trimmed);
  if (url.isValid() && url.isLocalFile()) {
    return QDir::cleanPath(url.toLocalFile());
  }
  return QDir::cleanPath(trimmed);
}

SegmentationResult alpha_mask_from_image_file(const QString& file_path) {
  SegmentationResult result;
  QImageReader reader(file_path);
  reader.setAutoTransform(true);
  auto image = reader.read();
  if (image.isNull()) {
    result.error_message = reader.errorString().isEmpty()
                               ? QStringLiteral("Unable to read alpha mask image.")
                               : reader.errorString();
    return result;
  }
  if (image.width() <= 0 || image.height() <= 0) {
    result.error_message = QStringLiteral("Alpha mask image is empty.");
    return result;
  }

  constexpr int kMaxAlphaMaskEdge = 2048;
  if (image.width() > kMaxAlphaMaskEdge || image.height() > kMaxAlphaMaskEdge) {
    image = image.scaled(
        kMaxAlphaMaskEdge,
        kMaxAlphaMaskEdge,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
  }

  const auto has_alpha = image.hasAlphaChannel();
  const auto raster = image.convertToFormat(QImage::Format_RGBA8888);
  result.width = raster.width();
  result.height = raster.height();
  result.mask_data = QByteArray(result.width * result.height, '\0');
  auto has_pixels = false;
  for (int y = 0; y < result.height; ++y) {
    for (int x = 0; x < result.width; ++x) {
      const auto color = raster.pixelColor(x, y);
      const auto alpha = has_alpha
                             ? color.alpha()
                             : qBound(
                                   0,
                                   static_cast<int>(std::lround(
                                       (0.2126 * color.red()) +
                                       (0.7152 * color.green()) +
                                       (0.0722 * color.blue()))),
                                   255);
      result.mask_data[y * result.width + x] = static_cast<char>(alpha);
      has_pixels = has_pixels || alpha > 0;
    }
  }
  result.success = has_pixels;
  if (!result.success) {
    result.error_message = QStringLiteral("Alpha mask image contains no selected pixels.");
  }
  return result;
}

QString with_preview_revision(const QString& source, int revision) {
  if (source.isEmpty()) {
    return {};
  }

  auto url = QUrl(source);
  if (!url.isValid() || url.scheme().isEmpty()) {
    return source;
  }

  QUrlQuery query(url);
  query.removeAllQueryItems(QStringLiteral("rev"));
  query.addQueryItem(QStringLiteral("rev"), QString::number(revision));
  url.setQuery(query);
  return url.toString();
}

QColor brush_preview_color_for_mask(
    const QVariantMap& mask,
    const QString& preview_mode) {
  const auto normalized_mode = preview_mode.trimmed().toLower();
  if (normalized_mode == QStringLiteral("grayscale") ||
      normalized_mode == QStringLiteral("gray")) {
    return QColor(QStringLiteral("#edf1f4"));
  }
  if (normalized_mode == QStringLiteral("black-on-white") ||
      normalized_mode == QStringLiteral("black on white")) {
    return QColor(QStringLiteral("#101419"));
  }
  if (normalized_mode == QStringLiteral("white-on-black") ||
      normalized_mode == QStringLiteral("white on black")) {
    return QColor(QStringLiteral("#f4f7fa"));
  }
  if (normalized_mode == QStringLiteral("edge") || normalized_mode == QStringLiteral("edge-view") ||
      normalized_mode == QStringLiteral("edge view") ||
      normalized_mode == QStringLiteral("marching-ants") ||
      normalized_mode == QStringLiteral("marching ants")) {
    return QColor(QStringLiteral("#f6f3ed"));
  }
  if (normalized_mode == QStringLiteral("affected") ||
      normalized_mode == QStringLiteral("affected-pixels") ||
      normalized_mode == QStringLiteral("affected pixels")) {
    return QColor(QStringLiteral("#f0c987"));
  }

  Q_UNUSED(mask);
  return QColor(QStringLiteral("#cf3b32"));
}

double brush_preview_opacity_for_mask(
    const QVariantMap& mask,
    const QString& preview_mode) {
  const auto normalized_mode = preview_mode.trimmed().toLower();
  if (normalized_mode == QStringLiteral("grayscale") ||
      normalized_mode == QStringLiteral("gray")) {
    return 0.72;
  }
  if (normalized_mode == QStringLiteral("black-on-white") ||
      normalized_mode == QStringLiteral("black on white") ||
      normalized_mode == QStringLiteral("white-on-black") ||
      normalized_mode == QStringLiteral("white on black")) {
    return 0.92;
  }
  if (normalized_mode == QStringLiteral("edge") || normalized_mode == QStringLiteral("edge-view") ||
      normalized_mode == QStringLiteral("edge view") ||
      normalized_mode == QStringLiteral("affected") ||
      normalized_mode == QStringLiteral("affected-pixels") ||
      normalized_mode == QStringLiteral("affected pixels")) {
    return 0.88;
  }
  if (normalized_mode == QStringLiteral("marching-ants") ||
      normalized_mode == QStringLiteral("marching ants")) {
    return 0.0;
  }
  Q_UNUSED(mask);
  return 0.30;
}

bool edit_states_equal(const EditState& left, const EditState& right) {
  return left == right;
}

bool render_request_superseded(const RenderRequest& request) {
  return request.latest_generation != nullptr &&
         request.generation < request.latest_generation->load(std::memory_order_relaxed);
}

bool preview_supported(const QString& file_path) {
  const QFileInfo file_info(file_path);
  const auto suffix = file_info.suffix().toLower().toUtf8();
  return QImageReader::supportedImageFormats().contains(suffix) || raw_image_supported(file_path) ||
         native_image_supported(file_path);
}

QSize source_image_size_for_file(const QString& file_path) {
  if (!preview_supported(file_path)) {
    return {};
  }

  if (native_image_supported(file_path)) {
    return native_image_size(file_path);
  }

  QImageReader reader(file_path);
  if (reader.canRead()) {
    return reader.size();
  }

  return raw_image_size(file_path);
}

QRect normalized_rect_to_image_rect(const QSize& size, const QRectF& normalized_rect) {
  if (!size.isValid()) {
    return {};
  }

  const auto normalized = normalized_rect.intersected(QRectF(0.0, 0.0, 1.0, 1.0));
  if (!normalized.isValid() || normalized.width() <= 0.0 || normalized.height() <= 0.0) {
    return {};
  }

  const auto x = qBound(
      0,
      static_cast<int>(std::floor(normalized.x() * static_cast<double>(size.width()))),
      size.width() - 1);
  const auto y = qBound(
      0,
      static_cast<int>(std::floor(normalized.y() * static_cast<double>(size.height()))),
      size.height() - 1);
  const auto right = qBound(
      x + 1,
      static_cast<int>(std::ceil(normalized.right() * static_cast<double>(size.width()))),
      size.width());
  const auto bottom = qBound(
      y + 1,
      static_cast<int>(std::ceil(normalized.bottom() * static_cast<double>(size.height()))),
      size.height());
  return QRect(x, y, right - x, bottom - y);
}

double clamp_signed_percent(double value) {
  return qBound(-100.0, value, 100.0);
}

double clamp_channel_mixer_percent(double value) {
  return qBound(-200.0, value, 200.0);
}

double clamp_unit(double value) {
  return qBound(0.0, value, 1.0);
}

double smoothstep(double edge0, double edge1, double value) {
  if (edge0 == edge1) {
    return value < edge0 ? 0.0 : 1.0;
  }
  const auto t = clamp_unit((value - edge0) / (edge1 - edge0));
  return t * t * (3.0 - 2.0 * t);
}

struct WhiteBalancePatchSample final {
  double red = 0.0;
  double green = 0.0;
  double blue = 0.0;
  int sample_count = 0;
  bool valid = false;
};

WhiteBalancePatchSample sample_white_balance_patch(
    const QImage& image,
    int center_x,
    int center_y,
    int radius) {
  WhiteBalancePatchSample result;
  if (image.isNull() || radius < 0) {
    return result;
  }

  std::vector<std::array<double, 3>> samples;
  samples.reserve(static_cast<std::size_t>((radius * 2 + 1) * (radius * 2 + 1)));

  const auto append_samples = [&](bool reject_extremes) {
    samples.clear();
    for (int y = std::max(0, center_y - radius); y <= std::min(image.height() - 1, center_y + radius); ++y) {
      for (int x = std::max(0, center_x - radius); x <= std::min(image.width() - 1, center_x + radius); ++x) {
        const QColor sampled(image.pixel(x, y));
        const auto red = photo_editor::pipeline::srgb_to_linear(sampled.redF());
        const auto green = photo_editor::pipeline::srgb_to_linear(sampled.greenF());
        const auto blue = photo_editor::pipeline::srgb_to_linear(sampled.blueF());
        const auto luminance = red * 0.2126 + green * 0.7152 + blue * 0.0722;
        const auto max_channel = std::max({red, green, blue});
        if (reject_extremes &&
            (luminance <= 1e-4 || luminance >= 0.98 || max_channel >= 0.995)) {
          continue;
        }
        samples.push_back({red, green, blue});
      }
    }
  };

  append_samples(true);
  if (samples.empty()) {
    append_samples(false);
  }
  if (samples.empty()) {
    return result;
  }

  std::sort(samples.begin(), samples.end(), [](const auto& left, const auto& right) {
    const auto left_luma = left[0] * 0.2126 + left[1] * 0.7152 + left[2] * 0.0722;
    const auto right_luma = right[0] * 0.2126 + right[1] * 0.7152 + right[2] * 0.0722;
    return left_luma < right_luma;
  });

  const auto trim = samples.size() >= 9 ? samples.size() / 8 : std::size_t{0};
  const auto begin = trim;
  const auto end = samples.size() > trim ? samples.size() - trim : samples.size();
  if (begin >= end) {
    return result;
  }

  auto red_sum = 0.0;
  auto green_sum = 0.0;
  auto blue_sum = 0.0;
  for (auto index = begin; index < end; ++index) {
    red_sum += samples[index][0];
    green_sum += samples[index][1];
    blue_sum += samples[index][2];
  }

  result.sample_count = static_cast<int>(end - begin);
  result.red = red_sum / static_cast<double>(result.sample_count);
  result.green = green_sum / static_cast<double>(result.sample_count);
  result.blue = blue_sum / static_cast<double>(result.sample_count);
  result.valid = true;
  return result;
}

bool map_bool_value(const QVariantMap& map, const QString& key, bool fallback) {
  return map.contains(key) ? map.value(key).toBool() : fallback;
}

double map_double_value(const QVariantMap& map, const QString& key, double fallback) {
  return map.contains(key) ? map.value(key).toDouble() : fallback;
}

QString pipeline_mask_tool_role(const QVariantMap& mask) {
  if (mask.contains(QStringLiteral("toolRole"))) {
    return normalized_mask_tool_role(mask.value(QStringLiteral("toolRole")).toString());
  }

  const auto retouch_mode =
      mask.value(QStringLiteral("retouchMode")).toString().trimmed().toLower();
  if (!retouch_mode.isEmpty()) {
    return QStringLiteral("retouch");
  }

  const auto has_selection_adjustments =
      std::abs(map_double_value(mask, QStringLiteral("exposure"), 0.0)) > 1e-6 ||
      std::abs(map_double_value(mask, QStringLiteral("temperature"), 0.0)) > 1e-6 ||
      std::abs(map_double_value(mask, QStringLiteral("hueShift"), 0.0)) > 1e-6 ||
      std::abs(map_double_value(mask, QStringLiteral("saturationShift"), 0.0)) > 1e-6 ||
      std::abs(map_double_value(mask, QStringLiteral("colorizeHue"), 0.0)) > 1e-6 ||
      std::abs(map_double_value(mask, QStringLiteral("colorizeStrength"), 0.0)) > 1e-6;
  if (!has_selection_adjustments &&
      std::abs(map_double_value(mask, QStringLiteral("removeStrength"), 0.0)) > 1e-6) {
    return QStringLiteral("retouch");
  }

  return QStringLiteral("selection");
}

QString normalized_pick_intent(const QString& intent) {
  const auto normalized = intent.trimmed().toLower();
  if (normalized == QStringLiteral("remove")) {
    return normalized;
  }
  if (normalized == QStringLiteral("selection-subtract") ||
      normalized == QStringLiteral("subtract")) {
    return QStringLiteral("selection-subtract");
  }
  return QStringLiteral("selection");
}

QString pick_intent_mask_operation(const QString& intent) {
  return normalized_pick_intent(intent) == QStringLiteral("selection-subtract")
             ? QStringLiteral("subtract")
             : QStringLiteral("add");
}

bool is_selection_mask(const QVariantMap& mask) {
  return normalized_mask_tool_role(mask.value(QStringLiteral("toolRole")).toString()) ==
         QStringLiteral("selection");
}

bool is_retouch_mask(const QVariantMap& mask) {
  return normalized_mask_tool_role(mask.value(QStringLiteral("toolRole")).toString()) ==
         QStringLiteral("retouch");
}

QVariantList retouch_before_after_mask_stack(const QVariantList& masks) {
  QVariantList result;
  result.reserve(masks.size());
  for (const auto& value : masks) {
    auto mask = value.toMap();
    if (is_retouch_mask(mask)) {
      mask.insert(QStringLiteral("enabled"), false);
      mask.insert(QStringLiteral("solo"), false);
    }
    result.append(mask);
  }
  return result;
}

void copy_selection_adjustments_to_mask(QVariantMap* target, const QVariantMap& source) {
  if (target == nullptr) {
    return;
  }
  copy_selection_adjustments(target, source);
  target->insert(
      QStringLiteral("enabled"),
      source.contains(QStringLiteral("enabled"))
          ? source.value(QStringLiteral("enabled")).toBool()
          : true);
  target->insert(
      QStringLiteral("maskSetName"),
      source.value(QStringLiteral("maskSetName")).toString());
}

void inherit_selection_mask_set(QVariantMap* target, const QVariantMap& source) {
  if (target == nullptr || !is_selection_mask(source)) {
    return;
  }
  target->insert(QStringLiteral("maskSetId"), source.value(QStringLiteral("maskSetId")).toString());
  target->insert(
      QStringLiteral("maskSetName"),
      source.value(QStringLiteral("maskSetName")).toString());
  copy_selection_adjustments_to_mask(target, source);
}

QVariantList grouped_mask_sets(const QVariantList& masks) {
  QVariantList grouped;
  QHash<QString, int> indices_by_id;
  QHash<QString, QString> mask_names_by_id;
  auto name_ordinal = 1;
  for (const auto& value : masks) {
    const auto mask = normalized_mask_definition(value.toMap(), name_ordinal);
    ++name_ordinal;
    const auto id = mask.value(QStringLiteral("id")).toString();
    if (!id.isEmpty()) {
      mask_names_by_id.insert(id, mask.value(QStringLiteral("name")).toString());
    }
  }
  auto ordinal = 1;
  for (const auto& value : masks) {
    const auto mask = normalized_mask_definition(value.toMap(), ordinal);
    ++ordinal;
    if (!is_selection_mask(mask)) {
      continue;
    }

    const auto set_id = mask.value(QStringLiteral("maskSetId")).toString();
    auto set_index = indices_by_id.value(set_id, -1);
    if (set_index < 0) {
      QVariantMap set;
      set.insert(QStringLiteral("id"), set_id);
      set.insert(
          QStringLiteral("name"),
          mask.value(QStringLiteral("maskSetName")).toString());
      set.insert(QStringLiteral("enabled"), mask.value(QStringLiteral("enabled")).toBool());
      set.insert(QStringLiteral("exposure"), mask.value(QStringLiteral("exposure")).toDouble());
      set.insert(
          QStringLiteral("temperature"),
          mask.value(QStringLiteral("temperature")).toDouble());
      set.insert(QStringLiteral("hueShift"), mask.value(QStringLiteral("hueShift")).toDouble());
      set.insert(
          QStringLiteral("saturationShift"),
          mask.value(QStringLiteral("saturationShift")).toDouble());
      set.insert(
          QStringLiteral("colorizeHue"),
          mask.value(QStringLiteral("colorizeHue")).toDouble());
      set.insert(
          QStringLiteral("colorizeStrength"),
          mask.value(QStringLiteral("colorizeStrength")).toDouble());
      set.insert(QStringLiteral("sources"), QVariantList{});
      grouped.append(set);
      set_index = static_cast<int>(grouped.size()) - 1;
      indices_by_id.insert(set_id, set_index);
    }

    auto set = grouped.at(set_index).toMap();
    auto sources = set.value(QStringLiteral("sources")).toList();
    auto source = mask;
    source.insert(QStringLiteral("flatIndex"), ordinal - 1);
    QStringList dependency_parts;
    const auto parent_id = source.value(QStringLiteral("parentId")).toString();
    if (!parent_id.isEmpty()) {
      dependency_parts.push_back(
          QStringLiteral("Depends on %1").arg(mask_names_by_id.value(parent_id, parent_id)));
    }
    if (source.value(QStringLiteral("reusableOutput")).toBool()) {
      const auto output_id = source.value(QStringLiteral("outputId")).toString();
      dependency_parts.push_back(
          output_id.isEmpty() ? QStringLiteral("Reusable output")
                              : QStringLiteral("Reusable output: %1").arg(output_id));
    }
    source.insert(QStringLiteral("dependencyLabel"), dependency_parts.join(QStringLiteral(" · ")));
    sources.append(source);
    set.insert(QStringLiteral("sources"), sources);
    set.insert(QStringLiteral("sourceCount"), sources.size());
    grouped[set_index] = set;
  }
  return grouped;
}

QString retouch_mode_display_name(const QString& mode_name) {
  const auto mode = mode_name.trimmed().toLower();
  if (mode == QStringLiteral("remove")) {
    return QStringLiteral("Remove");
  }
  if (mode == QStringLiteral("heal")) {
    return QStringLiteral("Heal");
  }
  if (mode == QStringLiteral("patch")) {
    return QStringLiteral("Patch");
  }
  if (mode == QStringLiteral("clone")) {
    return QStringLiteral("Clone");
  }
  if (mode == QStringLiteral("dust")) {
    return QStringLiteral("Dust");
  }
  if (mode == QStringLiteral("red-eye")) {
    return QStringLiteral("Red Eye");
  }
  if (mode == QStringLiteral("skin-cleanup")) {
    return QStringLiteral("Skin Cleanup");
  }
  if (mode == QStringLiteral("frequency-smoothing")) {
    return QStringLiteral("Frequency Smoothing");
  }
  return QStringLiteral("Retouch");
}

QVariantList grouped_retouch_stack(const QVariantList& masks) {
  QVariantList grouped;
  auto ordinal = 1;
  for (const auto& value : masks) {
    auto mask = normalized_mask_definition(value.toMap(), ordinal);
    if (is_retouch_mask(mask)) {
      const auto point_count = mask.value(QStringLiteral("brushPoints")).toList().size();
      const auto committed = mask.value(QStringLiteral("committed")).toBool();
      const auto mode_label =
          retouch_mode_display_name(mask.value(QStringLiteral("retouchMode")).toString());
      QStringList audit_parts;
      audit_parts.push_back(committed ? QStringLiteral("applied") : QStringLiteral("staged"));
      if (!mask.value(QStringLiteral("enabled")).toBool()) {
        audit_parts.push_back(QStringLiteral("off"));
      }
      audit_parts.push_back(
          point_count > 0 ? QStringLiteral("%1 pts").arg(point_count)
                          : QStringLiteral("no strokes"));
      const auto candidate_count = mask.value(QStringLiteral("inpaintCandidateCount")).toInt();
      if (candidate_count > 0) {
        audit_parts.push_back(QStringLiteral("%1 candidates").arg(candidate_count));
      }
      if (mask.value(QStringLiteral("inpaintFallback")).toBool()) {
        audit_parts.push_back(QStringLiteral("fallback"));
      }
      mask.insert(QStringLiteral("flatIndex"), ordinal - 1);
      mask.insert(QStringLiteral("retouchModeLabel"), mode_label);
      mask.insert(QStringLiteral("retouchStatus"), committed ? QStringLiteral("applied")
                                                             : QStringLiteral("staged"));
      mask.insert(QStringLiteral("retouchPointCount"), point_count);
      mask.insert(QStringLiteral("retouchAuditLabel"), mode_label + QStringLiteral(" · ") +
                                                        audit_parts.join(QStringLiteral(" · ")));
      grouped.append(mask);
    }
    ++ordinal;
  }
  return grouped;
}

QVariantList normalized_photo_layers(const QVariantList& layers) {
  QVariantList normalized_layers;
  normalized_layers.reserve(layers.size());
  auto ordinal = 1;
  for (const auto& value : layers) {
    auto layer = value.toMap();
    if (layer.isEmpty()) {
      continue;
    }

    auto kind = layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
    if (kind != QStringLiteral("pixel") && kind != QStringLiteral("adjustment") &&
        kind != QStringLiteral("fill") && kind != QStringLiteral("text") &&
        kind != QStringLiteral("source") && kind != QStringLiteral("group")) {
      kind = QStringLiteral("adjustment");
    }
    layer.insert(QStringLiteral("kind"), kind);
    if (layer.value(QStringLiteral("id")).toString().trimmed().isEmpty()) {
      layer.insert(QStringLiteral("id"), QStringLiteral("layer-%1").arg(ordinal));
    }
    if (layer.value(QStringLiteral("name")).toString().trimmed().isEmpty()) {
      layer.insert(
          QStringLiteral("name"),
          kind == QStringLiteral("pixel") ? QStringLiteral("Pixel Layer %1").arg(ordinal)
          : kind == QStringLiteral("fill") ? QStringLiteral("Fill Layer %1").arg(ordinal)
          : kind == QStringLiteral("text") ? QStringLiteral("Text Layer %1").arg(ordinal)
          : kind == QStringLiteral("source") ? QStringLiteral("Source Layer %1").arg(ordinal)
          : kind == QStringLiteral("group") ? QStringLiteral("Group %1").arg(ordinal)
                                          : QStringLiteral("Adjustment Layer %1").arg(ordinal));
    }
    layer.insert(
        QStringLiteral("enabled"),
        layer.contains(QStringLiteral("enabled"))
            ? layer.value(QStringLiteral("enabled")).toBool()
            : true);
    layer.insert(QStringLiteral("locked"), layer.value(QStringLiteral("locked")).toBool());
    layer.insert(QStringLiteral("solo"), layer.value(QStringLiteral("solo")).toBool());
    layer.insert(
        QStringLiteral("opacity"),
        qBound(0.0, layer.value(QStringLiteral("opacity"), 1.0).toDouble(), 1.0));
    layer.insert(QStringLiteral("clippingMask"), layer.value(QStringLiteral("clippingMask")).toBool());
    layer.insert(QStringLiteral("x"), qBound(-1000000, layer.value(QStringLiteral("x")).toInt(), 1000000));
    layer.insert(QStringLiteral("y"), qBound(-1000000, layer.value(QStringLiteral("y")).toInt(), 1000000));
    layer.insert(
        QStringLiteral("scale"),
        qBound(0.05, layer.value(QStringLiteral("scale"), 1.0).toDouble(), 32.0));
    layer.insert(
        QStringLiteral("rotation"),
        qBound(-3600.0, layer.value(QStringLiteral("rotation")).toDouble(), 3600.0));
    const auto blend_mode = layer.value(QStringLiteral("blendMode")).toString().trimmed().toLower();
    layer.insert(
        QStringLiteral("blendMode"),
        blend_mode.isEmpty() ? QStringLiteral("normal") : blend_mode);
    auto fill_color = QColor(layer.value(QStringLiteral("fillColor"), QStringLiteral("#808080")).toString());
    if (!fill_color.isValid()) {
      fill_color = QColor(QStringLiteral("#808080"));
    }
    layer.insert(QStringLiteral("fillColor"), fill_color.name(QColor::HexRgb));
    layer.insert(
        QStringLiteral("fillAlpha"),
        qBound(0.0, layer.value(QStringLiteral("fillAlpha"), 1.0).toDouble(), 1.0));
    if (kind == QStringLiteral("text")) {
      if (layer.value(QStringLiteral("text")).toString().isEmpty()) {
        layer.insert(QStringLiteral("text"), QStringLiteral("Text"));
      }
      auto text_color = QColor(layer.value(QStringLiteral("textColor"), QStringLiteral("#ffffff")).toString());
      if (!text_color.isValid()) {
        text_color = QColor(QStringLiteral("#ffffff"));
      }
      layer.insert(QStringLiteral("textColor"), text_color.name(QColor::HexRgb));
      layer.insert(QStringLiteral("fontSize"), qBound(8, layer.value(QStringLiteral("fontSize"), 48).toInt(), 512));
      layer.insert(QStringLiteral("pixelWidth"), qBound(16, layer.value(QStringLiteral("pixelWidth"), 512).toInt(), 8192));
      layer.insert(QStringLiteral("pixelHeight"), qBound(16, layer.value(QStringLiteral("pixelHeight"), 128).toInt(), 8192));
    }
    if (kind == QStringLiteral("source")) {
      layer.insert(QStringLiteral("sourcePath"), layer.value(QStringLiteral("sourcePath")).toString());
      layer.insert(QStringLiteral("pixelWidth"), qBound(0, layer.value(QStringLiteral("pixelWidth")).toInt(), 8192));
      layer.insert(QStringLiteral("pixelHeight"), qBound(0, layer.value(QStringLiteral("pixelHeight")).toInt(), 8192));
    }
    if (kind == QStringLiteral("group") &&
        layer.value(QStringLiteral("groupId")).toString().trimmed().isEmpty()) {
      layer.insert(QStringLiteral("groupId"), layer.value(QStringLiteral("id")).toString());
    }
    const auto opacity_percent =
        qRound(qBound(0.0, layer.value(QStringLiteral("opacity"), 1.0).toDouble(), 1.0) * 100.0);
    const auto kind_label =
        kind == QStringLiteral("pixel") ? QStringLiteral("Pixel")
        : kind == QStringLiteral("fill") ? QStringLiteral("Fill")
        : kind == QStringLiteral("text") ? QStringLiteral("Text")
        : kind == QStringLiteral("source") ? QStringLiteral("Source")
        : kind == QStringLiteral("group") ? QStringLiteral("Group")
                                        : QStringLiteral("Adjustment");
    QStringList audit_parts;
    audit_parts.push_back(kind_label);
    audit_parts.push_back(layer.value(QStringLiteral("enabled")).toBool() ? QStringLiteral("visible")
                                                                           : QStringLiteral("hidden"));
    if (layer.value(QStringLiteral("solo")).toBool()) {
      audit_parts.push_back(QStringLiteral("solo"));
    }
    if (layer.value(QStringLiteral("locked")).toBool()) {
      audit_parts.push_back(QStringLiteral("locked"));
    }
    if (layer.value(QStringLiteral("clippingMask")).toBool()) {
      audit_parts.push_back(QStringLiteral("clips below"));
    }
    const auto has_layer_mask =
        layer.contains(QStringLiteral("maskData")) || layer.value(QStringLiteral("maskWidth")).toInt() > 0;
    if (has_layer_mask) {
      audit_parts.push_back(QStringLiteral("mask"));
    }
    audit_parts.push_back(QStringLiteral("%1%").arg(opacity_percent));
    audit_parts.push_back(layer.value(QStringLiteral("blendMode")).toString());
    layer.insert(QStringLiteral("flatIndex"), ordinal - 1);
    layer.insert(QStringLiteral("kindLabel"), kind_label);
    layer.insert(QStringLiteral("opacityPercent"), opacity_percent);
    layer.insert(QStringLiteral("hasLayerMask"), has_layer_mask);
    layer.insert(
        QStringLiteral("layerMaskLabel"),
        has_layer_mask
            ? (layer.value(QStringLiteral("maskInverted")).toBool() ? QStringLiteral("Layer mask inverted")
                                                                     : QStringLiteral("Layer mask"))
            : QString());
    layer.insert(QStringLiteral("layerAuditLabel"), audit_parts.join(QStringLiteral(" · ")));
    layer.insert(
        QStringLiteral("dependencyLabel"),
        layer.value(QStringLiteral("clippingMask")).toBool()
            ? QStringLiteral("Clips to layer below")
        : !layer.value(QStringLiteral("groupId")).toString().trimmed().isEmpty() &&
                kind != QStringLiteral("group")
            ? QStringLiteral("Group: %1").arg(layer.value(QStringLiteral("groupId")).toString())
        : layer.contains(QStringLiteral("maskData")) || layer.value(QStringLiteral("maskWidth")).toInt() > 0
            ? QStringLiteral("Layer mask")
            : QString());
    normalized_layers.append(layer);
    ++ordinal;
  }
  return normalized_layers;
}

QRectF photo_layer_bounds_rect(const QVariantMap& layer) {
  const auto width = qMax(0.0, layer.value(QStringLiteral("pixelWidth")).toDouble());
  const auto height = qMax(0.0, layer.value(QStringLiteral("pixelHeight")).toDouble());
  const auto scale = qBound(0.05, layer.value(QStringLiteral("scale"), 1.0).toDouble(), 32.0);
  return QRectF(
      layer.value(QStringLiteral("x")).toDouble(),
      layer.value(QStringLiteral("y")).toDouble(),
      width * scale,
      height * scale);
}

QVariantMap photo_layer_bounds_map(const QVariantMap& layer) {
  const auto bounds = photo_layer_bounds_rect(layer);
  QVariantMap map;
  map.insert(QStringLiteral("x"), bounds.x());
  map.insert(QStringLiteral("y"), bounds.y());
  map.insert(QStringLiteral("width"), bounds.width());
  map.insert(QStringLiteral("height"), bounds.height());
  map.insert(QStringLiteral("right"), bounds.right());
  map.insert(QStringLiteral("bottom"), bounds.bottom());
  map.insert(QStringLiteral("centerX"), bounds.center().x());
  map.insert(QStringLiteral("centerY"), bounds.center().y());
  map.insert(QStringLiteral("scale"), qBound(0.05, layer.value(QStringLiteral("scale"), 1.0).toDouble(), 32.0));
  map.insert(QStringLiteral("rotation"), qBound(-3600.0, layer.value(QStringLiteral("rotation")).toDouble(), 3600.0));
  return map;
}

bool photo_layer_has_geometry(const QVariantMap& layer) {
  const auto kind = layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
  return (kind == QStringLiteral("pixel") || kind == QStringLiteral("text") ||
          kind == QStringLiteral("source")) &&
         photo_layer_bounds_rect(layer).width() > 0.0 && photo_layer_bounds_rect(layer).height() > 0.0;
}

QImage rasterized_text_layer_image(const QVariantMap& layer);

QString normalized_export_file_path(const QString& file_path) {
  const auto trimmed = file_path.trimmed();
  if (trimmed.isEmpty()) {
    return {};
  }
  const QUrl url(trimmed);
  if (url.isValid() && url.isLocalFile()) {
    return QDir::cleanPath(url.toLocalFile());
  }
  return QDir::cleanPath(QFileInfo(trimmed).absoluteFilePath());
}

QByteArray image_writer_format_for_path(const QString& file_path, const QByteArray& fallback = "png") {
  const auto suffix = QFileInfo(file_path).suffix().trimmed().toLower();
  if (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg")) {
    return "jpg";
  }
  if (suffix == QStringLiteral("tif") || suffix == QStringLiteral("tiff")) {
    return "tif";
  }
  if (suffix == QStringLiteral("png")) {
    return "png";
  }
  return fallback;
}

bool write_export_image(QImage image, const QString& file_path, const QByteArray& fallback_format = "png") {
  const auto normalized_path = normalized_export_file_path(file_path);
  if (normalized_path.isEmpty() || image.isNull()) {
    return false;
  }
  if (!QDir().mkpath(QFileInfo(normalized_path).absolutePath())) {
    return false;
  }
  const auto format = image_writer_format_for_path(normalized_path, fallback_format);
  if (format == "jpg") {
    QImage opaque(image.size(), QImage::Format_RGB32);
    opaque.fill(Qt::white);
    QPainter painter(&opaque);
    painter.drawImage(0, 0, image);
    image = opaque;
  }
  QImageWriter writer(normalized_path, format);
  if (format == "jpg") {
    writer.setQuality(95);
  }
  return writer.write(image);
}

void multiply_image_alpha(QImage* image, double opacity) {
  if (image == nullptr || image->isNull()) {
    return;
  }
  const auto alpha = qBound(0.0, opacity, 1.0);
  if (alpha >= 1.0 - 1e-6) {
    return;
  }
  *image = image->convertToFormat(QImage::Format_RGBA8888);
  for (int y = 0; y < image->height(); ++y) {
    for (int x = 0; x < image->width(); ++x) {
      auto color = image->pixelColor(x, y);
      color.setAlphaF(static_cast<float>(qBound(0.0, color.alphaF() * alpha, 1.0)));
      image->setPixelColor(x, y, color);
    }
  }
}

QImage transformed_layer_export_image(QImage image, const QVariantMap& layer) {
  if (image.isNull()) {
    return {};
  }
  if (!image.colorSpace().isValid()) {
    image.setColorSpace(preview_target_color_space());
  }
  const auto scale = qBound(0.05, layer.value(QStringLiteral("scale"), 1.0).toDouble(), 32.0);
  if (std::abs(scale - 1.0) > 1e-6) {
    image = image.scaled(
        qMax(1, qRound(static_cast<double>(image.width()) * scale)),
        qMax(1, qRound(static_cast<double>(image.height()) * scale)),
        Qt::IgnoreAspectRatio,
        Qt::SmoothTransformation);
  }
  const auto rotation = qBound(-3600.0, layer.value(QStringLiteral("rotation")).toDouble(), 3600.0);
  if (std::abs(rotation) > 1e-6) {
    QTransform transform;
    transform.rotate(rotation);
    image = image.transformed(transform, Qt::SmoothTransformation);
  }
  multiply_image_alpha(&image, layer.value(QStringLiteral("opacity"), 1.0).toDouble());
  return image;
}

QImage export_image_for_photo_layer(const QVariantMap& layer, QSize canvas_size) {
  const auto kind = layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
  QImage image;
  if (kind == QStringLiteral("pixel")) {
    image = QImage::fromData(layer.value(QStringLiteral("pixelData")).toByteArray(), "PNG");
  } else if (kind == QStringLiteral("text")) {
    image = rasterized_text_layer_image(layer);
  } else if (kind == QStringLiteral("source")) {
    image = QImage(layer.value(QStringLiteral("sourcePath")).toString());
    const auto target_width = layer.value(QStringLiteral("pixelWidth")).toInt();
    const auto target_height = layer.value(QStringLiteral("pixelHeight")).toInt();
    if (!image.isNull() && target_width > 0 && target_height > 0 &&
        (image.width() != target_width || image.height() != target_height)) {
      image = image.scaled(target_width, target_height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
  } else if (kind == QStringLiteral("fill")) {
    const auto width = qMax(1, layer.value(QStringLiteral("pixelWidth"), canvas_size.width()).toInt());
    const auto height = qMax(1, layer.value(QStringLiteral("pixelHeight"), canvas_size.height()).toInt());
    auto fill = QColor(layer.value(QStringLiteral("fillColor"), QStringLiteral("#808080")).toString());
    if (!fill.isValid()) {
      fill = QColor(QStringLiteral("#808080"));
    }
    fill.setAlphaF(static_cast<float>(
        qBound(0.0, layer.value(QStringLiteral("fillAlpha"), 1.0).toDouble(), 1.0)));
    image = QImage(width, height, QImage::Format_RGBA8888);
    image.fill(fill);
  }
  return transformed_layer_export_image(std::move(image), layer);
}

QVector<int> normalized_photo_layer_indices(const QVariantList& indices, qsizetype layer_count) {
  QVector<int> normalized;
  QSet<int> seen;
  normalized.reserve(indices.size());
  for (const auto& value : indices) {
    const auto index = value.toInt();
    if (index < 0 || index >= layer_count || seen.contains(index)) {
      continue;
    }
    normalized.push_back(index);
    seen.insert(index);
  }
  return normalized;
}

QRectF union_photo_layer_bounds(const QVariantList& layers, const QVector<int>& indices) {
  QRectF bounds;
  auto initialized = false;
  for (const auto index : indices) {
    const auto layer = layers.at(index).toMap();
    if (!photo_layer_has_geometry(layer)) {
      continue;
    }
    const auto layer_bounds = photo_layer_bounds_rect(layer);
    bounds = initialized ? bounds.united(layer_bounds) : layer_bounds;
    initialized = true;
  }
  return bounds;
}

QVariantMap adjustment_layer_from_state(
    const EditState& state,
    const QString& name,
    int next_index) {
  QVariantMap layer;
  const auto layer_name = name.simplified().isEmpty()
                              ? QStringLiteral("Adjustment Layer %1").arg(next_index)
                              : name.simplified();
  layer.insert(
      QStringLiteral("id"),
      QStringLiteral("adjustment-%1-%2")
          .arg(QDateTime::currentMSecsSinceEpoch())
          .arg(next_index));
  layer.insert(QStringLiteral("name"), layer_name);
  layer.insert(QStringLiteral("kind"), QStringLiteral("adjustment"));
  layer.insert(QStringLiteral("enabled"), true);
  layer.insert(QStringLiteral("opacity"), 1.0);
  layer.insert(QStringLiteral("blendMode"), QStringLiteral("normal"));
  layer.insert(QStringLiteral("exposure"), state.exposure);
  layer.insert(QStringLiteral("contrast"), state.contrast);
  layer.insert(QStringLiteral("highlights"), state.highlights);
  layer.insert(QStringLiteral("shadows"), state.shadows);
  layer.insert(QStringLiteral("whites"), state.whites);
  layer.insert(QStringLiteral("blacks"), state.blacks);
  layer.insert(QStringLiteral("temperature"), state.temperature);
  layer.insert(QStringLiteral("tint"), state.tint);
  layer.insert(QStringLiteral("vibrance"), state.vibrance);
  layer.insert(QStringLiteral("saturation"), state.saturation);
  layer.insert(QStringLiteral("hueShift"), state.hue_adjustment);
  layer.insert(QStringLiteral("saturationShift"), state.saturation_adjustment);
  layer.insert(QStringLiteral("colorizeHue"), 0.0);
  layer.insert(QStringLiteral("colorizeStrength"), 0.0);
  return layer;
}

QVariantMap fill_layer_definition(const QString& requested_color, const QString& name, int next_index) {
  auto color = QColor(requested_color.simplified().isEmpty() ? QStringLiteral("#808080")
                                                             : requested_color.simplified());
  if (!color.isValid()) {
    color = QColor(QStringLiteral("#808080"));
  }
  QVariantMap layer;
  layer.insert(
      QStringLiteral("id"),
      QStringLiteral("fill-%1-%2")
          .arg(QDateTime::currentMSecsSinceEpoch())
          .arg(next_index));
  layer.insert(
      QStringLiteral("name"),
      name.simplified().isEmpty() ? QStringLiteral("Fill Layer %1").arg(next_index)
                                  : name.simplified());
  layer.insert(QStringLiteral("kind"), QStringLiteral("fill"));
  layer.insert(QStringLiteral("enabled"), true);
  layer.insert(QStringLiteral("opacity"), 1.0);
  layer.insert(QStringLiteral("blendMode"), QStringLiteral("normal"));
  layer.insert(QStringLiteral("fillColor"), color.name(QColor::HexRgb));
  layer.insert(QStringLiteral("fillAlpha"), color.alphaF());
  return layer;
}

QVariantMap text_layer_definition(const QString& requested_text, const QString& name, int next_index) {
  const auto text = requested_text.trimmed().isEmpty() ? QStringLiteral("Text") : requested_text;
  QVariantMap layer;
  layer.insert(
      QStringLiteral("id"),
      QStringLiteral("text-%1-%2")
          .arg(QDateTime::currentMSecsSinceEpoch())
          .arg(next_index));
  layer.insert(
      QStringLiteral("name"),
      name.simplified().isEmpty() ? QStringLiteral("Text Layer %1").arg(next_index)
                                  : name.simplified());
  layer.insert(QStringLiteral("kind"), QStringLiteral("text"));
  layer.insert(QStringLiteral("enabled"), true);
  layer.insert(QStringLiteral("opacity"), 1.0);
  layer.insert(QStringLiteral("blendMode"), QStringLiteral("normal"));
  layer.insert(QStringLiteral("text"), text);
  layer.insert(QStringLiteral("textColor"), QStringLiteral("#ffffff"));
  layer.insert(QStringLiteral("fontFamily"), QStringLiteral("Helvetica"));
  layer.insert(QStringLiteral("fontSize"), 48);
  layer.insert(QStringLiteral("fontWeight"), QStringLiteral("regular"));
  layer.insert(QStringLiteral("pixelWidth"), 512);
  layer.insert(QStringLiteral("pixelHeight"), 128);
  layer.insert(QStringLiteral("x"), 64);
  layer.insert(QStringLiteral("y"), 64);
  return layer;
}

QVariantMap source_layer_definition(const QString& file_path, const QString& name, int next_index) {
  const QFileInfo source_info(file_path);
  QImageReader reader(source_info.absoluteFilePath());
  const auto image_size = reader.size();
  QVariantMap layer;
  layer.insert(
      QStringLiteral("id"),
      QStringLiteral("source-%1-%2")
          .arg(QDateTime::currentMSecsSinceEpoch())
          .arg(next_index));
  layer.insert(
      QStringLiteral("name"),
      name.simplified().isEmpty() ? source_info.completeBaseName() : name.simplified());
  layer.insert(QStringLiteral("kind"), QStringLiteral("source"));
  layer.insert(QStringLiteral("enabled"), true);
  layer.insert(QStringLiteral("opacity"), 1.0);
  layer.insert(QStringLiteral("blendMode"), QStringLiteral("normal"));
  layer.insert(QStringLiteral("sourcePath"), source_info.absoluteFilePath());
  layer.insert(QStringLiteral("pixelWidth"), image_size.isValid() ? image_size.width() : 0);
  layer.insert(QStringLiteral("pixelHeight"), image_size.isValid() ? image_size.height() : 0);
  layer.insert(QStringLiteral("x"), 0);
  layer.insert(QStringLiteral("y"), 0);
  return layer;
}

QVariantMap layer_group_definition(const QString& name, int next_index) {
  const auto group_id = QStringLiteral("group-%1-%2")
                            .arg(QDateTime::currentMSecsSinceEpoch())
                            .arg(next_index);
  QVariantMap layer;
  layer.insert(QStringLiteral("id"), group_id);
  layer.insert(QStringLiteral("groupId"), group_id);
  layer.insert(
      QStringLiteral("name"),
      name.simplified().isEmpty() ? QStringLiteral("Group %1").arg(next_index)
                                  : name.simplified());
  layer.insert(QStringLiteral("kind"), QStringLiteral("group"));
  layer.insert(QStringLiteral("enabled"), true);
  layer.insert(QStringLiteral("opacity"), 1.0);
  layer.insert(QStringLiteral("blendMode"), QStringLiteral("normal"));
  layer.insert(QStringLiteral("expanded"), true);
  return layer;
}

int clamp_preview_target_edge(int edge) {
  if (edge <= 0) {
    return kDefaultInteractivePreviewEdge;
  }
  return qBound(kMinimumInteractivePreviewEdge, edge, kMaximumInteractivePreviewEdge);
}

QSize scaled_size_for_max_edge(const QSize& source_size, int max_edge) {
  if (!source_size.isValid() || max_edge <= 0) {
    return {};
  }
  return source_size.scaled(max_edge, max_edge, Qt::KeepAspectRatio);
}

QByteArray union_segmentation_masks(
    const QByteArray& existing_data,
    int existing_width,
    int existing_height,
    const QByteArray& incoming_data,
    int incoming_width,
    int incoming_height) {
  if (!valid_segmentation_payload(incoming_data, incoming_width, incoming_height)) {
    return existing_data;
  }

  if (!valid_segmentation_payload(existing_data, existing_width, existing_height) ||
      existing_width != incoming_width || existing_height != incoming_height) {
    return incoming_data;
  }

  QByteArray merged(existing_data);
  const auto size = std::min(merged.size(), incoming_data.size());
  for (int index = 0; index < size; ++index) {
    merged[index] = static_cast<char>(std::max(
        static_cast<unsigned char>(merged.at(index)),
        static_cast<unsigned char>(incoming_data.at(index))));
  }
  return merged;
}

struct DustSpotCandidate final {
  int x = 0;
  int y = 0;
  double radius = 0.0;
  double score = 0.0;
};

double dust_luminance_for_pixel(const QColor& color) {
  const auto red = photo_editor::pipeline::srgb_to_linear(color.redF());
  const auto green = photo_editor::pipeline::srgb_to_linear(color.greenF());
  const auto blue = photo_editor::pipeline::srgb_to_linear(color.blueF());
  return red * 0.2126 + green * 0.7152 + blue * 0.0722;
}

std::vector<DustSpotCandidate> detect_dust_spots(
    const QImage& image,
    int maximum_spots) {
  std::vector<DustSpotCandidate> spots;
  if (image.isNull() || image.width() < 16 || image.height() < 16 ||
      maximum_spots <= 0) {
    return spots;
  }

  const auto source = image.convertToFormat(QImage::Format_ARGB32);
  const auto width = source.width();
  const auto height = source.height();
  std::vector<double> luminance(static_cast<std::size_t>(width * height), 0.0);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      luminance[static_cast<std::size_t>(y * width + x)] =
          dust_luminance_for_pixel(QColor::fromRgb(source.pixel(x, y)));
    }
  }

  const auto luma_at = [&](int x, int y) {
    return luminance[static_cast<std::size_t>(y * width + x)];
  };

  std::vector<DustSpotCandidate> candidates;
  candidates.reserve(256);
  constexpr auto kMargin = 7;
  for (int y = kMargin; y < height - kMargin; ++y) {
    for (int x = kMargin; x < width - kMargin; ++x) {
      auto center_sum = 0.0;
      auto center_count = 0;
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          center_sum += luma_at(x + dx, y + dy);
          ++center_count;
        }
      }
      const auto center_mean = center_sum / static_cast<double>(center_count);

      auto ring_sum = 0.0;
      auto ring_square_sum = 0.0;
      auto ring_count = 0;
      for (int dy = -7; dy <= 7; ++dy) {
        for (int dx = -7; dx <= 7; ++dx) {
          const auto distance = std::hypot(static_cast<double>(dx), static_cast<double>(dy));
          if (distance < 3.5 || distance > 7.5) {
            continue;
          }
          const auto value = luma_at(x + dx, y + dy);
          ring_sum += value;
          ring_square_sum += value * value;
          ++ring_count;
        }
      }
      if (ring_count == 0) {
        continue;
      }

      const auto ring_mean = ring_sum / static_cast<double>(ring_count);
      const auto ring_variance =
          std::max(0.0, ring_square_sum / static_cast<double>(ring_count) - ring_mean * ring_mean);
      const auto ring_texture = std::sqrt(ring_variance);
      const auto contrast = ring_mean - center_mean;
      const auto threshold = std::max(0.035, ring_mean * 0.13);
      if (ring_mean < 0.08 || contrast < threshold || ring_texture > 0.11) {
        continue;
      }

      auto local_minimum = true;
      for (int dy = -2; dy <= 2 && local_minimum; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
          if (dx == 0 && dy == 0) {
            continue;
          }
          if (luma_at(x, y) > luma_at(x + dx, y + dy) + 0.003) {
            local_minimum = false;
            break;
          }
        }
      }
      if (!local_minimum) {
        continue;
      }

      candidates.push_back(DustSpotCandidate{
          .x = x,
          .y = y,
          .radius = std::clamp(2.4 + contrast * 18.0, 2.4, 7.5),
          .score = contrast * ring_mean * (1.0 - std::min(1.0, ring_texture / 0.11)),
      });
    }
  }

  std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
    return left.score > right.score;
  });

  const auto cluster_radius =
      std::clamp(static_cast<double>(std::min(width, height)) * 0.035, 10.0, 36.0);
  for (const auto& candidate : candidates) {
    auto clustered_candidate = candidate;
    auto weighted_x = 0.0;
    auto weighted_y = 0.0;
    auto weight_sum = 0.0;
    auto maximum_cluster_distance = 0.0;
    for (const auto& neighbor : candidates) {
      const auto distance =
          std::hypot(static_cast<double>(candidate.x - neighbor.x),
                     static_cast<double>(candidate.y - neighbor.y));
      if (distance > cluster_radius) {
        continue;
      }
      const auto weight = std::max(1e-6, neighbor.score);
      weighted_x += static_cast<double>(neighbor.x) * weight;
      weighted_y += static_cast<double>(neighbor.y) * weight;
      weight_sum += weight;
    }
    if (weight_sum > 0.0) {
      clustered_candidate.x = std::clamp(
          static_cast<int>(std::lround(weighted_x / weight_sum)), 0, width - 1);
      clustered_candidate.y = std::clamp(
          static_cast<int>(std::lround(weighted_y / weight_sum)), 0, height - 1);
      for (const auto& neighbor : candidates) {
        const auto distance_from_cluster =
            std::hypot(static_cast<double>(clustered_candidate.x - neighbor.x),
                       static_cast<double>(clustered_candidate.y - neighbor.y));
        const auto distance_from_seed =
            std::hypot(static_cast<double>(candidate.x - neighbor.x),
                       static_cast<double>(candidate.y - neighbor.y));
        if (distance_from_seed <= cluster_radius) {
          maximum_cluster_distance = std::max(maximum_cluster_distance, distance_from_cluster);
        }
      }
      clustered_candidate.radius = std::clamp(
          std::max(clustered_candidate.radius, maximum_cluster_distance * 0.65 + 4.0),
          2.4,
          24.0);
    }

    auto suppressed = false;
    for (const auto& accepted : spots) {
      const auto distance =
          std::hypot(static_cast<double>(clustered_candidate.x - accepted.x),
                     static_cast<double>(clustered_candidate.y - accepted.y));
      if (distance < std::max(6.0, clustered_candidate.radius + accepted.radius)) {
        suppressed = true;
        break;
      }
    }
    if (suppressed) {
      continue;
    }
    spots.push_back(clustered_candidate);
    if (static_cast<int>(spots.size()) >= maximum_spots) {
      break;
    }
  }
  return spots;
}

QByteArray dust_spot_mask_data(
    int width,
    int height,
    const std::vector<DustSpotCandidate>& spots) {
  QByteArray data(width * height, '\0');
  for (const auto& spot : spots) {
    const auto radius = std::max(1.5, spot.radius);
    const auto min_x = std::max(0, static_cast<int>(std::floor(static_cast<double>(spot.x) - radius * 1.35)));
    const auto max_x = std::min(width - 1, static_cast<int>(std::ceil(static_cast<double>(spot.x) + radius * 1.35)));
    const auto min_y = std::max(0, static_cast<int>(std::floor(static_cast<double>(spot.y) - radius * 1.35)));
    const auto max_y = std::min(height - 1, static_cast<int>(std::ceil(static_cast<double>(spot.y) + radius * 1.35)));
    for (int y = min_y; y <= max_y; ++y) {
      for (int x = min_x; x <= max_x; ++x) {
        const auto distance =
            std::hypot(static_cast<double>(x - spot.x), static_cast<double>(y - spot.y));
        const auto weight = 1.0 - smoothstep(0.42, 1.0, distance / radius);
        if (weight <= 1e-6) {
          continue;
        }
        const auto index = y * width + x;
        data[index] = static_cast<char>(std::max(
            static_cast<int>(static_cast<unsigned char>(data.at(index))),
            static_cast<int>(std::lround(clamp_unit(weight) * 255.0))));
      }
    }
  }
  return data;
}

struct SensorDustMapPayload final {
  QByteArray segmentation_data;
  int width = 0;
  int height = 0;
  double remove_strength = 74.0;
  double feather = 0.72;
  bool valid = false;
};

QString sensor_dust_map_key(const QString& camera_label, const QString& lens_model) {
  const auto identity =
      QStringLiteral("%1|%2")
          .arg(camera_label.simplified().isEmpty() ? QStringLiteral("unknown-camera") : camera_label.simplified(),
               lens_model.simplified().isEmpty() ? QStringLiteral("unknown-lens") : lens_model.simplified());
  return QString::fromLatin1(
      QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString sensor_dust_map_path(const QString& camera_label, const QString& lens_model) {
  const auto root = QDir(AppBootstrap::appDataLocation()).filePath(QStringLiteral("sensor-dust-maps"));
  return QDir(root).filePath(QStringLiteral("%1.json").arg(sensor_dust_map_key(camera_label, lens_model)));
}

bool write_sensor_dust_map(
    const QString& camera_label,
    const QString& lens_model,
    const QByteArray& segmentation_data,
    int width,
    int height,
    double remove_strength,
    double feather) {
  if (!valid_segmentation_payload(segmentation_data, width, height)) {
    return false;
  }

  const auto path = sensor_dust_map_path(camera_label, lens_model);
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
    return false;
  }

  QJsonObject object;
  object.insert(QStringLiteral("version"), 1);
  object.insert(QStringLiteral("cameraLabel"), camera_label);
  object.insert(QStringLiteral("lensModel"), lens_model);
  object.insert(QStringLiteral("width"), width);
  object.insert(QStringLiteral("height"), height);
  object.insert(QStringLiteral("removeStrength"), remove_strength);
  object.insert(QStringLiteral("feather"), feather);
  object.insert(QStringLiteral("segmentationData"), QString::fromLatin1(segmentation_data.toBase64()));

  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
  return file.commit();
}

SensorDustMapPayload read_sensor_dust_map(const QString& camera_label, const QString& lens_model) {
  SensorDustMapPayload payload;
  QFile file(sensor_dust_map_path(camera_label, lens_model));
  if (!file.open(QIODevice::ReadOnly)) {
    return payload;
  }

  const auto document = QJsonDocument::fromJson(file.readAll());
  if (!document.isObject()) {
    return payload;
  }
  const auto object = document.object();
  payload.width = object.value(QStringLiteral("width")).toInt();
  payload.height = object.value(QStringLiteral("height")).toInt();
  payload.remove_strength = object.value(QStringLiteral("removeStrength")).toDouble(74.0);
  payload.feather = object.value(QStringLiteral("feather")).toDouble(0.72);
  payload.segmentation_data =
      QByteArray::fromBase64(object.value(QStringLiteral("segmentationData")).toString().toLatin1());
  payload.valid = valid_segmentation_payload(payload.segmentation_data, payload.width, payload.height);
  return payload;
}

QByteArray resample_segmentation_mask(
    const QByteArray& source_data,
    int source_width,
    int source_height,
    int target_width,
    int target_height) {
  if (!valid_segmentation_payload(source_data, source_width, source_height) ||
      target_width <= 0 || target_height <= 0) {
    return {};
  }
  if (source_width == target_width && source_height == target_height) {
    return source_data;
  }

  QByteArray target_data(target_width * target_height, '\0');
  for (int y = 0; y < target_height; ++y) {
    const auto source_y =
        target_height > 1
            ? static_cast<double>(y) * static_cast<double>(source_height - 1) /
                  static_cast<double>(target_height - 1)
            : 0.0;
    const auto y0 = std::clamp(static_cast<int>(std::floor(source_y)), 0, source_height - 1);
    const auto y1 = std::min(source_height - 1, y0 + 1);
    const auto fy = source_y - static_cast<double>(y0);
    for (int x = 0; x < target_width; ++x) {
      const auto source_x =
          target_width > 1
              ? static_cast<double>(x) * static_cast<double>(source_width - 1) /
                    static_cast<double>(target_width - 1)
              : 0.0;
      const auto x0 = std::clamp(static_cast<int>(std::floor(source_x)), 0, source_width - 1);
      const auto x1 = std::min(source_width - 1, x0 + 1);
      const auto fx = source_x - static_cast<double>(x0);
      const auto sample = [&](int sample_x, int sample_y) {
        return static_cast<double>(
            static_cast<unsigned char>(source_data.at(sample_y * source_width + sample_x)));
      };
      const auto top = std::lerp(sample(x0, y0), sample(x1, y0), fx);
      const auto bottom = std::lerp(sample(x0, y1), sample(x1, y1), fx);
      target_data[y * target_width + x] =
          static_cast<char>(std::lround(std::lerp(top, bottom, fy)));
    }
  }
  return target_data;
}

struct RedEyeMaskResult final {
  QByteArray mask_data;
  int width = 0;
  int height = 0;
  int component_count = 0;
};

RedEyeMaskResult detect_red_eye_mask(const QImage& image) {
  RedEyeMaskResult result;
  if (image.isNull() || image.width() < 8 || image.height() < 8) {
    return result;
  }

  const auto source = image.convertToFormat(QImage::Format_ARGB32);
  const auto width = source.width();
  const auto height = source.height();
  result.width = width;
  result.height = height;
  result.mask_data = QByteArray(width * height, '\0');

  std::vector<uint8_t> candidate(static_cast<std::size_t>(width * height), 0);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto color = QColor::fromRgb(source.pixel(x, y));
      const auto red = color.redF();
      const auto green = color.greenF();
      const auto blue = color.blueF();
      const auto max_green_blue = std::max(green, blue);
      const auto min_green_blue = std::min(green, blue);
      if (red > 0.34 && red > max_green_blue * 1.55 &&
          red - max_green_blue > 0.16 && max_green_blue - min_green_blue < 0.22) {
        candidate[static_cast<std::size_t>(y * width + x)] = 1;
      }
    }
  }

  std::vector<uint8_t> visited(static_cast<std::size_t>(width * height), 0);
  std::queue<int> queue;
  const auto pixel_count = width * height;
  const auto max_component_area = std::max(16, pixel_count / 20);
  for (int index = 0; index < pixel_count; ++index) {
    if (!candidate[static_cast<std::size_t>(index)] || visited[static_cast<std::size_t>(index)]) {
      continue;
    }

    visited[static_cast<std::size_t>(index)] = 1;
    queue.push(index);
    auto area = 0;
    auto min_x = width - 1;
    auto max_x = 0;
    auto min_y = height - 1;
    auto max_y = 0;
    while (!queue.empty()) {
      const auto current = queue.front();
      queue.pop();
      const auto x = current % width;
      const auto y = current / width;
      ++area;
      min_x = std::min(min_x, x);
      max_x = std::max(max_x, x);
      min_y = std::min(min_y, y);
      max_y = std::max(max_y, y);

      const std::array<int, 4> neighbors = {current - 1, current + 1, current - width, current + width};
      for (const auto neighbor : neighbors) {
        if (neighbor < 0 || neighbor >= pixel_count) {
          continue;
        }
        const auto neighbor_x = neighbor % width;
        if (std::abs(neighbor_x - x) > 1) {
          continue;
        }
        const auto neighbor_index = static_cast<std::size_t>(neighbor);
        if (!candidate[neighbor_index] || visited[neighbor_index]) {
          continue;
        }
        visited[neighbor_index] = 1;
        queue.push(neighbor);
      }
    }

    const auto box_width = max_x - min_x + 1;
    const auto box_height = max_y - min_y + 1;
    const auto aspect = static_cast<double>(box_width) / static_cast<double>(std::max(1, box_height));
    if (area < 8 || area > max_component_area || aspect < 0.35 || aspect > 2.8) {
      continue;
    }

    const auto center_x = (static_cast<double>(min_x) + static_cast<double>(max_x)) * 0.5;
    const auto center_y = (static_cast<double>(min_y) + static_cast<double>(max_y)) * 0.5;
    const auto radius_x = std::max(2.0, static_cast<double>(box_width) * 0.78);
    const auto radius_y = std::max(2.0, static_cast<double>(box_height) * 0.78);
    const auto raster_min_x = std::max(0, static_cast<int>(std::floor(center_x - radius_x * 1.15)));
    const auto raster_max_x = std::min(width - 1, static_cast<int>(std::ceil(center_x + radius_x * 1.15)));
    const auto raster_min_y = std::max(0, static_cast<int>(std::floor(center_y - radius_y * 1.15)));
    const auto raster_max_y = std::min(height - 1, static_cast<int>(std::ceil(center_y + radius_y * 1.15)));
    for (int y = raster_min_y; y <= raster_max_y; ++y) {
      for (int x = raster_min_x; x <= raster_max_x; ++x) {
        const auto dx = (static_cast<double>(x) - center_x) / radius_x;
        const auto dy = (static_cast<double>(y) - center_y) / radius_y;
        const auto distance = std::sqrt(dx * dx + dy * dy);
        const auto weight = 1.0 - smoothstep(0.72, 1.0, distance);
        if (weight <= 1e-6) {
          continue;
        }
        const auto mask_index = y * width + x;
        result.mask_data[mask_index] = static_cast<char>(std::max(
            static_cast<int>(static_cast<unsigned char>(result.mask_data.at(mask_index))),
            static_cast<int>(std::lround(weight * 255.0))));
      }
    }
    ++result.component_count;
  }

  if (result.component_count == 0) {
    result.mask_data.clear();
    result.width = 0;
    result.height = 0;
  }
  return result;
}

struct SkinMaskResult final {
  QByteArray mask_data;
  int width = 0;
  int height = 0;
  int pixel_count = 0;
};

SkinMaskResult detect_skin_cleanup_mask(const QImage& image) {
  SkinMaskResult result;
  if (image.isNull() || image.width() < 4 || image.height() < 4) {
    return result;
  }

  const auto source = image.convertToFormat(QImage::Format_ARGB32);
  result.width = source.width();
  result.height = source.height();
  result.mask_data = QByteArray(result.width * result.height, '\0');
  for (int y = 0; y < result.height; ++y) {
    for (int x = 0; x < result.width; ++x) {
      const auto color = QColor::fromRgb(source.pixel(x, y));
      const auto red = color.redF();
      const auto green = color.greenF();
      const auto blue = color.blueF();
      const auto hue = color.hslHueF() < 0.0 ? 0.0 : color.hslHueF();
      const auto saturation = color.hslSaturationF();
      const auto lightness = color.lightnessF();
      const auto warm_hue = hue <= 0.14 || hue >= 0.94;
      const auto skin_ratio =
          red > blue * 1.10 && green > blue * 0.82 && red > green * 0.86 && red < green * 1.65;
      if (warm_hue && skin_ratio && saturation >= 0.08 && saturation <= 0.72 &&
          lightness >= 0.18 && lightness <= 0.86) {
        result.mask_data[y * result.width + x] = static_cast<char>(210);
        ++result.pixel_count;
      }
    }
  }
  if (result.pixel_count == 0) {
    result.mask_data.clear();
    result.width = 0;
    result.height = 0;
  }
  return result;
}

int instance_id_from_map(
    const QByteArray& instance_data,
    int width,
    int height,
    double normalized_x,
    double normalized_y) {
  if (instance_data.isEmpty() || width <= 0 || height <= 0) {
    return 0;
  }

  const auto sample_instance = [&](double sample_y) {
    const auto x = std::clamp(
        static_cast<int>(std::lround(std::clamp(normalized_x, 0.0, 1.0) *
                                     std::max(width - 1, 0))),
        0,
        std::max(width - 1, 0));
    const auto y = std::clamp(
        static_cast<int>(std::lround(std::clamp(sample_y, 0.0, 1.0) *
                                     std::max(height - 1, 0))),
        0,
        std::max(height - 1, 0));
    return static_cast<int>(
        static_cast<unsigned char>(instance_data.at(y * width + x)));
  };

  const auto direct_instance = sample_instance(normalized_y);
  const auto flipped_instance = sample_instance(1.0 - normalized_y);
  if (direct_instance == 0) {
    return flipped_instance;
  }
  if (flipped_instance == 0 || direct_instance == flipped_instance) {
    return direct_instance;
  }
  return direct_instance;
}

int instance_pixel_count_in_map(
    const QByteArray& instance_data,
    int width,
    int height,
    int instance_id) {
  if (instance_data.isEmpty() || width <= 0 || height <= 0 || instance_id <= 0) {
    return 0;
  }

  auto pixel_count = 0;
  const auto total_pixels = std::min<qsizetype>(
      instance_data.size(),
      static_cast<qsizetype>(width) * static_cast<qsizetype>(height));
  for (qsizetype index = 0; index < total_pixels; ++index) {
    if (static_cast<int>(static_cast<unsigned char>(instance_data.at(index))) == instance_id) {
      ++pixel_count;
    }
  }
  return pixel_count;
}

int instance_id_at_map_point(
    const QByteArray& instance_data,
    int width,
    int height,
    int x,
    int y) {
  if (instance_data.isEmpty() || width <= 0 || height <= 0 ||
      x < 0 || y < 0 || x >= width || y >= height) {
    return 0;
  }
  return static_cast<int>(
      static_cast<unsigned char>(instance_data.at(y * width + x)));
}

QVector<QPoint> smart_select_seed_points(
    int width,
    int height,
    double normalized_x,
    double normalized_y) {
  QVector<QPoint> seeds;
  if (width <= 0 || height <= 0) {
    return seeds;
  }

  const auto x = qBound(
      0,
      static_cast<int>(std::lround(std::clamp(normalized_x, 0.0, 1.0) *
                                   std::max(width - 1, 0))),
      std::max(width - 1, 0));
  const auto y = qBound(
      0,
      static_cast<int>(std::lround(std::clamp(normalized_y, 0.0, 1.0) *
                                   std::max(height - 1, 0))),
      std::max(height - 1, 0));
  seeds.append(QPoint(x, y));

  const auto flipped_y = std::max(height - 1 - y, 0);
  if (flipped_y != y) {
    seeds.append(QPoint(x, flipped_y));
  }
  return seeds;
}

QVector<QPointF> normalized_smart_select_path(const QVariantList& points) {
  QVector<QPointF> normalized_points;
  normalized_points.reserve(points.size());
  for (const auto& value : points) {
    const auto point = value.toMap();
    if (!point.contains(QStringLiteral("x")) || !point.contains(QStringLiteral("y"))) {
      continue;
    }

    const auto normalized = QPointF(
        qBound(0.0, point.value(QStringLiteral("x")).toDouble(), 1.0),
        qBound(0.0, point.value(QStringLiteral("y")).toDouble(), 1.0));
    if (!normalized_points.isEmpty()) {
      const auto previous = normalized_points.constLast();
      const auto delta_x = normalized.x() - previous.x();
      const auto delta_y = normalized.y() - previous.y();
      if ((delta_x * delta_x) + (delta_y * delta_y) < 1e-6) {
        continue;
      }
    }
    normalized_points.append(normalized);
  }
  return normalized_points;
}

QVariantList quantized_smart_select_preview_path(const QVariantList& points) {
  const auto normalized_points = normalized_smart_select_path(points);
  if (normalized_points.isEmpty()) {
    return {};
  }

  constexpr double kPreviewQuantizationSteps = 512.0;
  QVariantList quantized_points;
  quantized_points.reserve(normalized_points.size());
  for (const auto& point : normalized_points) {
    QVariantMap quantized_point;
    quantized_point.insert(
        QStringLiteral("x"),
        qBound(
            0.0,
            std::round(point.x() * kPreviewQuantizationSteps) / kPreviewQuantizationSteps,
            1.0));
    quantized_point.insert(
        QStringLiteral("y"),
        qBound(
            0.0,
            std::round(point.y() * kPreviewQuantizationSteps) / kPreviewQuantizationSteps,
            1.0));

    if (!quantized_points.isEmpty() &&
        quantized_points.constLast().toMap() == quantized_point) {
      continue;
    }
    quantized_points.append(quantized_point);
  }
  return quantized_points;
}

QVector<QPointF> sampled_smart_select_path_points(const QVector<QPointF>& points) {
  if (points.isEmpty()) {
    return {};
  }

  QVector<QPointF> sampled_points;
  sampled_points.reserve(points.size() * 4);
  sampled_points.append(points.constFirst());

  constexpr double kMaxPointSpacing = 0.045;
  for (int index = 1; index < points.size(); ++index) {
    const auto previous = points.at(index - 1);
    const auto current = points.at(index);
    const auto dx = current.x() - previous.x();
    const auto dy = current.y() - previous.y();
    const auto distance = std::hypot(dx, dy);
    const auto step_count = std::max(1, static_cast<int>(std::ceil(distance / kMaxPointSpacing)));
    for (int step = 1; step <= step_count; ++step) {
      const auto t = static_cast<double>(step) / static_cast<double>(step_count);
      sampled_points.append(QPointF(
          qBound(0.0, previous.x() + dx * t, 1.0),
          qBound(0.0, previous.y() + dy * t, 1.0)));
    }
  }

  return sampled_points;
}

QVector<SmartSelectPrompt> smart_select_prompts_for_path(const QVariantList& points) {
  QVector<SmartSelectPrompt> prompts;
  const auto normalized_points =
      sampled_smart_select_path_points(normalized_smart_select_path(points));
  prompts.reserve(normalized_points.size());
  for (const auto& point : normalized_points) {
    prompts.append(SmartSelectPrompt{
        .x = point.x(),
        .y = point.y(),
        .negative = false,
    });
  }
  return prompts;
}

QVariantMap segmentation_mask_variant(
    const SegmentationResult& result,
    const QString& kind) {
  if (!valid_segmentation_payload(result.mask_data, result.width, result.height)) {
    return {};
  }

  QVariantMap mask;
  mask.insert(QStringLiteral("kind"), normalized_mask_kind(kind));
  mask.insert(QStringLiteral("segmentationData"), result.mask_data);
  mask.insert(QStringLiteral("segmentationWidth"), result.width);
  mask.insert(QStringLiteral("segmentationHeight"), result.height);
  return mask;
}

SegmentationResult segmentation_result_from_mask_variant(const QVariantMap& mask) {
  SegmentationResult result;
  result.mask_data = mask.value(QStringLiteral("segmentationData")).toByteArray();
  result.width = mask.value(QStringLiteral("segmentationWidth")).toInt();
  result.height = mask.value(QStringLiteral("segmentationHeight")).toInt();
  result.success = valid_segmentation_payload(result.mask_data, result.width, result.height);
  if (!result.success) {
    result.error_message = QStringLiteral("Invalid segmentation payload");
  }
  return result;
}

int segmentation_mask_value_at_normalized(
    const QByteArray& mask_data,
    int width,
    int height,
    double normalized_x,
    double normalized_y) {
  if (!valid_segmentation_payload(mask_data, width, height)) {
    return 0;
  }

  const auto x = qBound(
      0,
      static_cast<int>(std::lround(std::clamp(normalized_x, 0.0, 1.0) *
                                   std::max(width - 1, 0))),
      std::max(width - 1, 0));
  const auto y = qBound(
      0,
      static_cast<int>(std::lround(std::clamp(normalized_y, 0.0, 1.0) *
                                   std::max(height - 1, 0))),
      std::max(height - 1, 0));
  return static_cast<int>(
      static_cast<unsigned char>(mask_data.at(y * width + x)));
}

int segmentation_result_nonzero_count(const SegmentationResult& result) {
  if (!valid_segmentation_payload(result.mask_data, result.width, result.height)) {
    return 0;
  }

  auto count = 0;
  for (const auto value : result.mask_data) {
    if (static_cast<unsigned char>(value) > 0) {
      ++count;
    }
  }
  return count;
}

double segmentation_result_coverage(const SegmentationResult& result) {
  if (!valid_segmentation_payload(result.mask_data, result.width, result.height) ||
      result.width <= 0 || result.height <= 0) {
    return 0.0;
  }

  return static_cast<double>(segmentation_result_nonzero_count(result)) /
         static_cast<double>(result.width * result.height);
}

bool should_prefer_prompt_object_pick_result(
    const SegmentationResult& instance_result,
    const SegmentationResult& prompt_result,
    double normalized_x,
    double normalized_y) {
  if (!valid_segmentation_payload(prompt_result.mask_data, prompt_result.width, prompt_result.height)) {
    return false;
  }
  if (!valid_segmentation_payload(instance_result.mask_data, instance_result.width, instance_result.height)) {
    return true;
  }

  const auto instance_center_value = segmentation_mask_value_at_normalized(
      instance_result.mask_data,
      instance_result.width,
      instance_result.height,
      normalized_x,
      normalized_y);
  const auto prompt_center_value = segmentation_mask_value_at_normalized(
      prompt_result.mask_data,
      prompt_result.width,
      prompt_result.height,
      normalized_x,
      normalized_y);
  if (prompt_center_value >= 96 && instance_center_value < 96) {
    return true;
  }
  if (instance_center_value >= 96 && prompt_center_value < 96) {
    return false;
  }

  const auto instance_coverage = segmentation_result_coverage(instance_result);
  const auto prompt_coverage = segmentation_result_coverage(prompt_result);
  if (prompt_coverage <= 0.0) {
    return false;
  }
  if (instance_coverage <= 0.0) {
    return true;
  }

  const auto prompt_coverage_ratio = prompt_coverage / instance_coverage;
  if (prompt_coverage_ratio < 0.78 && prompt_coverage_ratio > 0.02 && prompt_center_value >= 64) {
    return true;
  }
  if (prompt_coverage_ratio > 2.5 && instance_center_value >= 64) {
    return false;
  }

  return prompt_center_value > instance_center_value + 48 &&
         prompt_coverage_ratio <= 1.5;
}

QRect localized_object_pick_bounds(
    const QSize& image_size,
    double normalized_x,
    double normalized_y) {
  if (!image_size.isValid() || image_size.width() < 2 || image_size.height() < 2) {
    return {};
  }

  const auto min_edge = std::min(image_size.width(), image_size.height());
  const auto crop_extent = qBound(
      48,
      static_cast<int>(std::lround(static_cast<double>(min_edge) * 0.26)),
      320);
  const auto crop_width = std::min(image_size.width(), crop_extent);
  const auto crop_height = std::min(image_size.height(), crop_extent);
  const auto center_x = qBound(
      0,
      static_cast<int>(std::lround(std::clamp(normalized_x, 0.0, 1.0) *
                                   static_cast<double>(std::max(image_size.width() - 1, 0)))),
      std::max(image_size.width() - 1, 0));
  const auto center_y = qBound(
      0,
      static_cast<int>(std::lround(std::clamp(normalized_y, 0.0, 1.0) *
                                   static_cast<double>(std::max(image_size.height() - 1, 0)))),
      std::max(image_size.height() - 1, 0));
  const auto left = qBound(0, center_x - crop_width / 2, image_size.width() - crop_width);
  const auto top = qBound(0, center_y - crop_height / 2, image_size.height() - crop_height);
  return QRect(left, top, crop_width, crop_height);
}

QRect segmentation_result_bounds(const SegmentationResult& result);

bool segmentation_result_is_localized_square_failure(
    const QImage& source_image,
    const SegmentationResult& result,
    double normalized_x,
    double normalized_y) {
  if (source_image.isNull() ||
      !valid_segmentation_payload(result.mask_data, result.width, result.height) ||
      result.width <= 0 || result.height <= 0) {
    return false;
  }

  const auto bounds = segmentation_result_bounds(result);
  const auto localized_bounds =
      localized_object_pick_bounds(source_image.size(), normalized_x, normalized_y);
  if (!bounds.isValid() || !localized_bounds.isValid()) {
    return false;
  }

  const auto bounds_area = static_cast<double>(bounds.width()) *
                           static_cast<double>(bounds.height());
  if (bounds_area <= 0.0) {
    return false;
  }
  const auto fill_ratio =
      static_cast<double>(segmentation_result_nonzero_count(result)) / bounds_area;
  const auto covers_localized_crop =
      bounds.width() >= static_cast<int>(std::lround(localized_bounds.width() * 0.88)) &&
      bounds.height() >= static_cast<int>(std::lround(localized_bounds.height() * 0.88)) &&
      std::abs(bounds.center().x() - localized_bounds.center().x()) <=
          std::max(4, localized_bounds.width() / 12) &&
      std::abs(bounds.center().y() - localized_bounds.center().y()) <=
          std::max(4, localized_bounds.height() / 12);

  return covers_localized_crop && fill_ratio >= 0.72;
}

SegmentationResult remap_localized_segmentation_result(
    const SegmentationResult& local_result,
    const QRect& bounds,
    const QSize& target_size) {
  if (!valid_segmentation_payload(local_result.mask_data, local_result.width, local_result.height) ||
      !bounds.isValid() || !target_size.isValid()) {
    return {};
  }

  SegmentationResult remapped;
  remapped.width = target_size.width();
  remapped.height = target_size.height();
  remapped.mask_data = QByteArray(remapped.width * remapped.height, '\0');

  const auto copy_width = std::min(bounds.width(), local_result.width);
  const auto copy_height = std::min(bounds.height(), local_result.height);
  for (int y = 0; y < copy_height; ++y) {
    const auto source_offset = y * local_result.width;
    const auto target_offset = (bounds.y() + y) * remapped.width + bounds.x();
    for (int x = 0; x < copy_width; ++x) {
      remapped.mask_data[target_offset + x] = local_result.mask_data.at(source_offset + x);
    }
  }

  remapped.success =
      valid_segmentation_payload(remapped.mask_data, remapped.width, remapped.height);
  if (!remapped.success) {
    remapped.error_message = QStringLiteral("Failed to remap localized object mask");
  }
  return remapped;
}

struct LocalColorSample final {
  double red = 0.0;
  double green = 0.0;
  double blue = 0.0;
};

LocalColorSample local_color_sample(const QImage& image, int x, int y) {
  if (image.isNull()) {
    return {};
  }
  const auto clamped_x = qBound(0, x, image.width() - 1);
  const auto clamped_y = qBound(0, y, image.height() - 1);
  const auto pixel = image.pixel(clamped_x, clamped_y);
  return LocalColorSample{
      .red = static_cast<double>(qRed(pixel)) / 255.0,
      .green = static_cast<double>(qGreen(pixel)) / 255.0,
      .blue = static_cast<double>(qBlue(pixel)) / 255.0,
  };
}

double local_color_distance(const LocalColorSample& left, const LocalColorSample& right) {
  const auto delta_red = left.red - right.red;
  const auto delta_green = left.green - right.green;
  const auto delta_blue = left.blue - right.blue;
  return std::sqrt(
      delta_red * delta_red * 0.30 +
      delta_green * delta_green * 0.59 +
      delta_blue * delta_blue * 0.11);
}

LocalColorSample average_local_patch_color(
    const QImage& image,
    int center_x,
    int center_y,
    int radius) {
  if (image.isNull()) {
    return {};
  }

  auto red = 0.0;
  auto green = 0.0;
  auto blue = 0.0;
  auto count = 0.0;
  for (int y = center_y - radius; y <= center_y + radius; ++y) {
    for (int x = center_x - radius; x <= center_x + radius; ++x) {
      const auto sample = local_color_sample(image, x, y);
      red += sample.red;
      green += sample.green;
      blue += sample.blue;
      count += 1.0;
    }
  }
  if (count <= 0.0) {
    return {};
  }
  return LocalColorSample{
      .red = red / count,
      .green = green / count,
      .blue = blue / count,
  };
}

LocalColorSample average_local_perimeter_color(const QImage& image) {
  if (image.isNull()) {
    return {};
  }

  auto red = 0.0;
  auto green = 0.0;
  auto blue = 0.0;
  auto count = 0.0;
  const auto step = std::max(1, std::min(image.width(), image.height()) / 20);
  for (int x = 0; x < image.width(); x += step) {
    const auto top = local_color_sample(image, x, 0);
    const auto bottom = local_color_sample(image, x, image.height() - 1);
    red += top.red + bottom.red;
    green += top.green + bottom.green;
    blue += top.blue + bottom.blue;
    count += 2.0;
  }
  for (int y = 0; y < image.height(); y += step) {
    const auto left = local_color_sample(image, 0, y);
    const auto right = local_color_sample(image, image.width() - 1, y);
    red += left.red + right.red;
    green += left.green + right.green;
    blue += left.blue + right.blue;
    count += 2.0;
  }
  if (count <= 0.0) {
    return {};
  }
  return LocalColorSample{
      .red = red / count,
      .green = green / count,
      .blue = blue / count,
  };
}

QRect segmentation_result_bounds(const SegmentationResult& result) {
  if (!valid_segmentation_payload(result.mask_data, result.width, result.height)) {
    return {};
  }

  auto min_x = result.width;
  auto min_y = result.height;
  auto max_x = -1;
  auto max_y = -1;
  for (int y = 0; y < result.height; ++y) {
    const auto row_offset = y * result.width;
    for (int x = 0; x < result.width; ++x) {
      if (static_cast<unsigned char>(result.mask_data.at(row_offset + x)) < 96) {
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
  return QRect(min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
}

SegmentationResult generate_localized_object_mask(
    const QImage& source_image,
    double normalized_x,
    double normalized_y);

bool single_point_prompt_result_is_plausible(
    const QImage& /*source_image*/,
    const SegmentationResult& result,
    double normalized_x,
    double normalized_y) {
  if (!valid_segmentation_payload(result.mask_data, result.width, result.height)) {
    return false;
  }

  const auto center_value = segmentation_mask_value_at_normalized(
      result.mask_data,
      result.width,
      result.height,
      normalized_x,
      normalized_y);
  if (center_value < 96) {
    return false;
  }

  const auto coverage = segmentation_result_coverage(result);
  if (!(coverage > 0.0 && coverage <= 0.92)) {
    return false;
  }

  const auto bounds = segmentation_result_bounds(result);
  return bounds.isValid() && bounds.width() > 1 && bounds.height() > 1;
}

SegmentationResult compute_single_point_object_pick_result(
    const QImage& source_image,
    double normalized_x,
    double normalized_y) {
  auto prompt_result = generate_object_mask(source_image, normalized_x, normalized_y);
  const auto localized_prompt_result =
      generate_localized_object_mask(source_image, normalized_x, normalized_y);
  if (should_prefer_prompt_object_pick_result(
          prompt_result,
          localized_prompt_result,
          normalized_x,
          normalized_y)) {
    prompt_result = localized_prompt_result;
  }
  if (!single_point_prompt_result_is_plausible(
          source_image,
          prompt_result,
          normalized_x,
          normalized_y)) {
    return {};
  }
  if (segmentation_result_is_localized_square_failure(
          source_image,
          prompt_result,
          normalized_x,
          normalized_y)) {
    return {};
  }
  return prompt_result;
}

SegmentationResult compute_smart_select_result(
    const QImage& source_image,
    const QVariantList& points) {
  const auto normalized_points = normalized_smart_select_path(points);
  if (!normalized_points.isEmpty()) {
    const auto ai_status = ai_model_runtime_status();
    if (ai_status.smart_selection_available) {
      auto ai_result =
          generate_ai_smart_selection_mask(source_image, smart_select_prompts_for_path(points));
      if (ai_result.success) {
        return ai_result;
      }
      if (ai_result.error_message.isEmpty()) {
        ai_result.error_message =
            QStringLiteral("AI smart selection did not return a mask.");
      }
      return ai_result;
    }
  }
  if (normalized_points.size() == 1) {
    const auto point = normalized_points.constFirst();
    return compute_single_point_object_pick_result(source_image, point.x(), point.y());
  }
  if (!normalized_points.isEmpty()) {
    return generate_smart_select_mask(source_image, smart_select_prompts_for_path(points));
  }
  return {};
}

SegmentationResult refine_localized_object_mask_by_seed_color(
    const QImage& localized_image,
    const SegmentationResult& local_result,
    double normalized_x,
    double normalized_y) {
  if (localized_image.isNull() || localized_image.width() < 2 || localized_image.height() < 2) {
    return local_result;
  }

  const auto has_local_mask =
      valid_segmentation_payload(local_result.mask_data, local_result.width, local_result.height) &&
      local_result.width == localized_image.width() &&
      local_result.height == localized_image.height();
  const auto search_mask =
      has_local_mask
          ? local_result.mask_data
          : QByteArray(localized_image.width() * localized_image.height(), static_cast<char>(0xff));

  const auto seed_x = qBound(
      0,
      static_cast<int>(std::lround(std::clamp(normalized_x, 0.0, 1.0) *
                                   static_cast<double>(std::max(localized_image.width() - 1, 0)))),
      std::max(localized_image.width() - 1, 0));
  const auto seed_y = qBound(
      0,
      static_cast<int>(std::lround(std::clamp(normalized_y, 0.0, 1.0) *
                                   static_cast<double>(std::max(localized_image.height() - 1, 0)))),
      std::max(localized_image.height() - 1, 0));
  const auto seed_color = average_local_patch_color(localized_image, seed_x, seed_y, 1);
  const auto perimeter_color = average_local_perimeter_color(localized_image);
  const auto seed_background_distance =
      local_color_distance(seed_color, perimeter_color);
  const auto color_threshold =
      qBound(0.05, seed_background_distance * 0.55, 0.20);

  QByteArray refined(localized_image.width() * localized_image.height(), '\0');
  QVector<uchar> visited(
      static_cast<qsizetype>(localized_image.width() * localized_image.height()),
      0);
  std::queue<QPoint> queue;
  queue.push(QPoint(seed_x, seed_y));

  auto refined_count = 0;
  while (!queue.empty()) {
    const auto point = queue.front();
    queue.pop();
    if (point.x() < 0 || point.y() < 0 ||
        point.x() >= localized_image.width() || point.y() >= localized_image.height()) {
      continue;
    }

    const auto index = point.y() * localized_image.width() + point.x();
    if (visited[static_cast<qsizetype>(index)] != 0) {
      continue;
    }
    visited[static_cast<qsizetype>(index)] = 1;

    const auto mask_value =
        static_cast<unsigned char>(search_mask.at(index));
    if (mask_value < 24) {
      continue;
    }

    const auto sample = local_color_sample(localized_image, point.x(), point.y());
    const auto distance_to_seed = local_color_distance(sample, seed_color);
    if (distance_to_seed > color_threshold &&
        !(mask_value > 160 && distance_to_seed <= color_threshold * 1.35)) {
      continue;
    }

    refined[index] = static_cast<char>(0xff);
    ++refined_count;
    queue.push(QPoint(point.x() - 1, point.y()));
    queue.push(QPoint(point.x() + 1, point.y()));
    queue.push(QPoint(point.x(), point.y() - 1));
    queue.push(QPoint(point.x(), point.y() + 1));
  }

  if (refined_count <= 0) {
    return local_result;
  }
  if (has_local_mask &&
      refined_count >= segmentation_result_nonzero_count(local_result)) {
    return local_result;
  }

  return SegmentationResult{
      .mask_data = refined,
      .width = localized_image.width(),
      .height = localized_image.height(),
      .success = true,
      .error_message = QString(),
  };
}

SegmentationResult generate_localized_object_mask(
    const QImage& source_image,
    double normalized_x,
    double normalized_y) {
  if (source_image.isNull() || source_image.width() < 2 || source_image.height() < 2) {
    return {};
  }

  const auto bounds =
      localized_object_pick_bounds(source_image.size(), normalized_x, normalized_y);
  if (!bounds.isValid()) {
    return {};
  }

  const auto localized_image = source_image.copy(bounds);
  if (localized_image.isNull()) {
    return {};
  }

  const auto center_x = qBound(
      0,
      static_cast<int>(std::lround(std::clamp(normalized_x, 0.0, 1.0) *
                                   static_cast<double>(std::max(source_image.width() - 1, 0)))),
      std::max(source_image.width() - 1, 0));
  const auto center_y = qBound(
      0,
      static_cast<int>(std::lround(std::clamp(normalized_y, 0.0, 1.0) *
                                   static_cast<double>(std::max(source_image.height() - 1, 0)))),
      std::max(source_image.height() - 1, 0));
  const auto localized_x =
      qBound(0.0,
             static_cast<double>(qBound(0, center_x - bounds.x(), bounds.width() - 1)) /
                 static_cast<double>(std::max(bounds.width() - 1, 1)),
             1.0);
  const auto localized_y =
      qBound(0.0,
             static_cast<double>(qBound(0, center_y - bounds.y(), bounds.height() - 1)) /
                 static_cast<double>(std::max(bounds.height() - 1, 1)),
             1.0);

  const auto local_result = generate_object_mask(localized_image, localized_x, localized_y);
  const auto refined_local_result = refine_localized_object_mask_by_seed_color(
      localized_image,
      local_result,
      localized_x,
      localized_y);
  return remap_localized_segmentation_result(
      refined_local_result,
      bounds,
      source_image.size());
}

bool ensure_brush_raster_mask(
    QVariantMap* mask,
    const QSize& raster_size) {
  if (mask == nullptr || !raster_size.isValid() || raster_size.width() <= 1 ||
      raster_size.height() <= 1) {
    return false;
  }

  const auto existing_data = mask->value(QStringLiteral("segmentationData")).toByteArray();
  const auto existing_width = mask->value(QStringLiteral("segmentationWidth")).toInt();
  const auto existing_height = mask->value(QStringLiteral("segmentationHeight")).toInt();
  if (valid_segmentation_payload(existing_data, existing_width, existing_height)) {
    return true;
  }

  mask->insert(
      QStringLiteral("segmentationData"),
      QByteArray(raster_size.width() * raster_size.height(), '\0'));
  mask->insert(QStringLiteral("segmentationWidth"), raster_size.width());
  mask->insert(QStringLiteral("segmentationHeight"), raster_size.height());
  return true;
}

QVariantMap brush_point_variant(
    double normalized_x,
    double normalized_y,
    double brush_size,
    double brush_flow,
    double pressure = 1.0,
    double tilt_x = 0.0,
    double tilt_y = 0.0,
    bool erase = false) {
  const auto clamped_pressure = clamp_unit(pressure);
  const auto clamped_tilt_x = std::clamp(tilt_x, -1.0, 1.0);
  const auto clamped_tilt_y = std::clamp(tilt_y, -1.0, 1.0);
  QVariantMap point;
  point.insert(QStringLiteral("x"), normalized_x);
  point.insert(QStringLiteral("y"), normalized_y);
  point.insert(QStringLiteral("size"), std::max(0.01, brush_size));
  point.insert(QStringLiteral("flow"), clamp_unit(brush_flow));
  point.insert(QStringLiteral("pressure"), clamped_pressure);
  point.insert(QStringLiteral("tiltX"), clamped_tilt_x);
  point.insert(QStringLiteral("tiltY"), clamped_tilt_y);
  if (erase) {
    point.insert(QStringLiteral("erase"), true);
  }
  return point;
}

QVariantMap brush_point_variant(const BrushPreviewPoint& point) {
  return brush_point_variant(
      point.x, point.y, point.size, point.flow, point.pressure, point.tilt_x, point.tilt_y, point.erase);
}

BrushPreviewPoint brush_preview_point(
    double normalized_x,
    double normalized_y,
    double brush_size,
    double brush_flow,
    double pressure = 1.0,
    double tilt_x = 0.0,
    double tilt_y = 0.0,
    bool erase = false) {
  const auto clamped_pressure = clamp_unit(pressure);
  const auto clamped_tilt_x = std::clamp(tilt_x, -1.0, 1.0);
  const auto clamped_tilt_y = std::clamp(tilt_y, -1.0, 1.0);
  const auto pressure_size = std::lerp(0.35, 1.0, clamped_pressure);
  const auto tilt_magnitude =
      std::min(1.0, std::hypot(clamped_tilt_x, clamped_tilt_y));
  const auto tilt_size = std::lerp(1.0, 1.35, tilt_magnitude);
  return BrushPreviewPoint{
      .x = normalized_x,
      .y = normalized_y,
      .size = std::max(0.01, brush_size * pressure_size * tilt_size),
      .flow = clamp_unit(brush_flow) * clamped_pressure,
      .pressure = clamped_pressure,
      .tilt_x = clamped_tilt_x,
      .tilt_y = clamped_tilt_y,
      .erase = erase,
  };
}

BrushPreviewPoint brush_preview_point_from_variant(
    const QVariant& value,
    double fallback_x,
    double fallback_y,
    double fallback_size,
    double fallback_flow) {
  const auto point = value.toMap();
  return BrushPreviewPoint{
      .x = map_double_value(point, QStringLiteral("x"), fallback_x),
      .y = map_double_value(point, QStringLiteral("y"), fallback_y),
      .size = map_double_value(point, QStringLiteral("size"), fallback_size),
      .flow = map_double_value(point, QStringLiteral("flow"), fallback_flow),
      .pressure = map_double_value(point, QStringLiteral("pressure"), 1.0),
      .tilt_x = map_double_value(point, QStringLiteral("tiltX"), 0.0),
      .tilt_y = map_double_value(point, QStringLiteral("tiltY"), 0.0),
      .erase = map_bool_value(point, QStringLiteral("erase"), false),
  };
}

QVector<BrushPreviewPoint> sampled_brush_points_for_segment(
    const BrushPreviewPoint* last_point,
    double normalized_x,
    double normalized_y,
    double brush_size,
    double brush_flow,
    double brush_spacing,
    double brush_smoothing,
    bool erase,
    double pressure = 1.0,
    double tilt_x = 0.0,
    double tilt_y = 0.0) {
  QVector<BrushPreviewPoint> sampled_points;
  const auto cursor_point =
      brush_preview_point(normalized_x, normalized_y, brush_size, brush_flow, pressure, tilt_x, tilt_y, erase);
  const auto smoothing_amount = std::clamp(brush_smoothing, 0.0, 1.0) * 0.85;
  if (last_point != nullptr) {
    normalized_x = std::lerp(normalized_x, last_point->x, smoothing_amount);
    normalized_y = std::lerp(normalized_y, last_point->y, smoothing_amount);
  }
  const auto clamped_point =
      brush_preview_point(normalized_x, normalized_y, brush_size, brush_flow, pressure, tilt_x, tilt_y, erase);

  if (last_point == nullptr) {
    sampled_points.append(cursor_point);
    return sampled_points;
  }

  const auto distance = std::hypot(clamped_point.x - last_point->x, clamped_point.y - last_point->y);
  const auto cursor_distance = std::hypot(cursor_point.x - last_point->x, cursor_point.y - last_point->y);
  const auto spacing =
      std::clamp(clamped_point.size * std::clamp(brush_spacing, 0.05, 2.0), 0.0025, 0.05);
  if (distance <= spacing * 0.35) {
    if (smoothing_amount > 0.0 && cursor_distance > spacing * 0.35) {
      sampled_points.append(cursor_point);
    }
    return sampled_points;
  }

  const auto steps = std::max(1, static_cast<int>(std::ceil(distance / spacing)));
  sampled_points.reserve(steps + 1);
  for (int step = 1; step <= steps; ++step) {
    const auto t = static_cast<double>(step) / static_cast<double>(steps);
    sampled_points.append(brush_preview_point(
        std::lerp(last_point->x, clamped_point.x, t),
        std::lerp(last_point->y, clamped_point.y, t),
        brush_size,
        brush_flow,
        std::lerp(last_point->pressure, clamped_point.pressure, t),
        std::lerp(last_point->tilt_x, clamped_point.tilt_x, t),
        std::lerp(last_point->tilt_y, clamped_point.tilt_y, t),
        erase));
  }
  const auto& sampled_endpoint = sampled_points.constLast();
  const auto endpoint_distance =
      std::hypot(cursor_point.x - sampled_endpoint.x, cursor_point.y - sampled_endpoint.y);
  if (smoothing_amount > 0.0 && endpoint_distance > 0.000001) {
    sampled_points.append(cursor_point);
  }
  return sampled_points;
}

void rasterize_brush_point_into_mask(
    QVariantMap* mask,
    const QImage* source_image,
    double normalized_x,
    double normalized_y,
    double brush_size,
    double brush_flow,
    double feather,
    bool erase = false,
    bool auto_mask = false,
    double auto_mask_tolerance = 0.18) {
  if (mask == nullptr) {
    return;
  }

  auto data = mask->value(QStringLiteral("segmentationData")).toByteArray();
  const auto width = mask->value(QStringLiteral("segmentationWidth")).toInt();
  const auto height = mask->value(QStringLiteral("segmentationHeight")).toInt();
  if (!valid_segmentation_payload(data, width, height)) {
    return;
  }

  const auto softness = std::clamp(feather, 0.05, 1.0);
  const auto inner_scale = std::max(0.0, 1.0 - softness);
  const auto clamped_size = std::max(0.01, brush_size);
  const auto clamped_flow = clamp_unit(brush_flow);
  const auto center_x = clamp_unit(normalized_x);
  const auto center_y = clamp_unit(normalized_y);
  const auto auto_mask_enabled =
      auto_mask && source_image != nullptr && !source_image->isNull() &&
      source_image->width() > 1 && source_image->height() > 1;
  const auto source_width = auto_mask_enabled ? source_image->width() : 0;
  const auto source_height = auto_mask_enabled ? source_image->height() : 0;
  const auto anchor_source_x = auto_mask_enabled
                                   ? std::clamp(static_cast<int>(std::lround(
                                                    center_x * static_cast<double>(source_width - 1))),
                                                0,
                                                source_width - 1)
                                   : 0;
  const auto anchor_source_y = auto_mask_enabled
                                   ? std::clamp(static_cast<int>(std::lround(
                                                    center_y * static_cast<double>(source_height - 1))),
                                                0,
                                                source_height - 1)
                                   : 0;
  const auto anchor_color =
      auto_mask_enabled ? QColor::fromRgb(source_image->pixel(anchor_source_x, anchor_source_y)) : QColor{};
  const auto clamped_auto_tolerance = std::clamp(auto_mask_tolerance, 0.02, 0.75);
  const auto max_x_radius = std::max(
      2,
      static_cast<int>(std::ceil(clamped_size * static_cast<double>(std::max(width - 1, 1)))));
  const auto max_y_radius = std::max(
      2,
      static_cast<int>(std::ceil(clamped_size * static_cast<double>(std::max(height - 1, 1)))));
  const auto anchor_x = clamp_unit(center_x) * static_cast<double>(std::max(width - 1, 1));
  const auto anchor_y = clamp_unit(center_y) * static_cast<double>(std::max(height - 1, 1));
  const auto min_x = std::max(0, static_cast<int>(std::floor(anchor_x)) - max_x_radius);
  const auto max_x = std::min(width - 1, static_cast<int>(std::ceil(anchor_x)) + max_x_radius);
  const auto min_y = std::max(0, static_cast<int>(std::floor(anchor_y)) - max_y_radius);
  const auto max_y = std::min(height - 1, static_cast<int>(std::ceil(anchor_y)) + max_y_radius);

  for (int y = min_y; y <= max_y; ++y) {
    for (int x = min_x; x <= max_x; ++x) {
      const auto pixel_x =
          width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
      const auto pixel_y =
          height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
      const auto delta_x = pixel_x - center_x;
      const auto delta_y = pixel_y - center_y;
      const auto distance = std::sqrt(delta_x * delta_x + delta_y * delta_y) / clamped_size;
      const auto weight =
          clamped_flow * (1.0 - smoothstep(inner_scale, 1.0, distance));
      auto adjusted_weight = weight;
      if (auto_mask_enabled) {
        const auto source_x = std::clamp(static_cast<int>(std::lround(
                                           pixel_x * static_cast<double>(source_width - 1))),
                                         0,
                                         source_width - 1);
        const auto source_y = std::clamp(static_cast<int>(std::lround(
                                           pixel_y * static_cast<double>(source_height - 1))),
                                         0,
                                         source_height - 1);
        const auto sample_color = QColor::fromRgb(source_image->pixel(source_x, source_y));
        const auto red_delta = sample_color.redF() - anchor_color.redF();
        const auto green_delta = sample_color.greenF() - anchor_color.greenF();
        const auto blue_delta = sample_color.blueF() - anchor_color.blueF();
        const auto color_distance = std::sqrt(
            red_delta * red_delta * 0.30 +
            green_delta * green_delta * 0.59 +
            blue_delta * blue_delta * 0.11);
        adjusted_weight *=
            1.0 - smoothstep(clamped_auto_tolerance, clamped_auto_tolerance * 2.5, color_distance);
      }
      if (adjusted_weight <= 1e-6) {
        continue;
      }
      const auto index = y * width + x;
      const auto existing =
          static_cast<unsigned char>(data.at(index));
      if (erase) {
        data[index] = static_cast<char>(
            std::lround(static_cast<double>(existing) * (1.0 - clamp_unit(adjusted_weight))));
      } else {
        data[index] = static_cast<char>(std::max(
            static_cast<int>(existing),
            static_cast<int>(std::lround(clamp_unit(adjusted_weight) * 255.0))));
      }
    }
  }

  mask->insert(QStringLiteral("segmentationData"), data);
}

QVector<int> smart_select_instance_candidates_from_map(
    const QByteArray& instance_data,
    int width,
    int height,
    double normalized_x,
    double normalized_y) {
  if (instance_data.isEmpty() || width <= 0 || height <= 0) {
    return {};
  }

  const auto direct_instance =
      instance_id_from_map(instance_data, width, height, normalized_x, normalized_y);
  if (direct_instance > 0) {
    return {direct_instance};
  }

  const auto max_radius = qBound(
      1,
      static_cast<int>(std::lround(static_cast<double>(std::min(width, height)) * 0.08)),
      24);

  const auto seeds = smart_select_seed_points(width, height, normalized_x, normalized_y);
  const auto collect_candidates =
      [&](const QVector<QPoint>& search_seeds) -> QVector<int> {
    QHash<int, int> best_distance_by_instance;
    auto first_hit_radius = -1;
    for (auto radius = 1; radius <= max_radius; ++radius) {
      for (const auto& seed : search_seeds) {
        for (int dy = -radius; dy <= radius; ++dy) {
          for (int dx = -radius; dx <= radius; ++dx) {
            if (std::max(std::abs(dx), std::abs(dy)) != radius) {
              continue;
            }
            const auto instance_id = instance_id_at_map_point(
                instance_data,
                width,
                height,
                seed.x() + dx,
                seed.y() + dy);
            if (instance_id <= 0) {
              continue;
            }
            const auto distance_sq = dx * dx + dy * dy;
            const auto existing_distance =
                best_distance_by_instance.value(instance_id, std::numeric_limits<int>::max());
            if (distance_sq < existing_distance) {
              best_distance_by_instance.insert(instance_id, distance_sq);
            }
          }
        }
      }

      if (!best_distance_by_instance.isEmpty()) {
        if (first_hit_radius < 0) {
          first_hit_radius = radius;
          continue;
        }
        if (radius < first_hit_radius + 1) {
          continue;
        }

        auto best_distance = std::numeric_limits<int>::max();
        for (auto it = best_distance_by_instance.cbegin();
             it != best_distance_by_instance.cend();
             ++it) {
          best_distance = std::min(best_distance, it.value());
        }

        const auto best_radius = std::sqrt(static_cast<double>(best_distance));
        const auto candidate_distance_limit =
            std::pow(best_radius + 1.0, 2.0);
        QVector<int> candidates;
        for (auto it = best_distance_by_instance.cbegin();
             it != best_distance_by_instance.cend();
             ++it) {
          if (static_cast<double>(it.value()) <= candidate_distance_limit) {
            candidates.append(it.key());
          }
        }
        std::sort(candidates.begin(), candidates.end(), [&](int left, int right) {
          const auto left_coverage =
              instance_pixel_count_in_map(instance_data, width, height, left);
          const auto right_coverage =
              instance_pixel_count_in_map(instance_data, width, height, right);
          if (left_coverage != right_coverage) {
            return left_coverage > right_coverage;
          }
          return left < right;
        });
        return candidates;
      }
    }

    if (!best_distance_by_instance.isEmpty()) {
      auto best_distance = std::numeric_limits<int>::max();
      for (auto it = best_distance_by_instance.cbegin();
           it != best_distance_by_instance.cend();
           ++it) {
        best_distance = std::min(best_distance, it.value());
      }

      const auto best_radius = std::sqrt(static_cast<double>(best_distance));
      const auto candidate_distance_limit =
          std::pow(best_radius + 1.0, 2.0);
      QVector<int> candidates;
      for (auto it = best_distance_by_instance.cbegin();
           it != best_distance_by_instance.cend();
           ++it) {
        if (static_cast<double>(it.value()) <= candidate_distance_limit) {
          candidates.append(it.key());
        }
      }
      std::sort(candidates.begin(), candidates.end(), [&](int left, int right) {
        const auto left_coverage =
            instance_pixel_count_in_map(instance_data, width, height, left);
        const auto right_coverage =
            instance_pixel_count_in_map(instance_data, width, height, right);
        if (left_coverage != right_coverage) {
          return left_coverage > right_coverage;
        }
        return left < right;
      });
      return candidates;
    }

    return {};
  };

  if (!seeds.isEmpty()) {
    const auto primary_candidates = collect_candidates({seeds.constFirst()});
    if (!primary_candidates.isEmpty()) {
      return primary_candidates;
    }
  }

  if (seeds.size() > 1) {
    const auto flipped_candidates = collect_candidates({seeds.constLast()});
    if (!flipped_candidates.isEmpty()) {
      return flipped_candidates;
    }
  }

  return {};
}

QVector<int> smart_select_instance_candidates_for_path(
    const QByteArray& instance_data,
    int width,
    int height,
    const QVector<QPointF>& points) {
  if (points.isEmpty()) {
    return {};
  }

  const auto sampled_points = sampled_smart_select_path_points(points);
  if (sampled_points.isEmpty()) {
    return {};
  }

  QHash<int, int> touch_count_by_instance;
  QHash<int, int> first_seen_by_instance;
  for (int point_index = 0; point_index < sampled_points.size(); ++point_index) {
    const auto point = sampled_points.at(point_index);
    const auto point_candidates = smart_select_instance_candidates_from_map(
        instance_data,
        width,
        height,
        point.x(),
        point.y());
    for (const auto instance_id : point_candidates) {
      if (instance_id <= 0) {
        continue;
      }
      touch_count_by_instance.insert(
          instance_id,
          touch_count_by_instance.value(instance_id, 0) + 1);
      if (!first_seen_by_instance.contains(instance_id)) {
        first_seen_by_instance.insert(instance_id, point_index);
      }
    }
  }

  if (touch_count_by_instance.isEmpty()) {
    return {};
  }

  auto max_touch_count = 0;
  for (auto it = touch_count_by_instance.cbegin(); it != touch_count_by_instance.cend(); ++it) {
    max_touch_count = std::max(max_touch_count, it.value());
  }

  const auto inclusion_threshold = std::max(
      1,
      static_cast<int>(std::ceil(static_cast<double>(max_touch_count) * 0.5)));
  QVector<int> candidates;
  for (auto it = touch_count_by_instance.cbegin(); it != touch_count_by_instance.cend(); ++it) {
    if (it.value() >= inclusion_threshold) {
      candidates.append(it.key());
    }
  }

  std::sort(candidates.begin(), candidates.end(), [&](int left, int right) {
    const auto left_touch_count = touch_count_by_instance.value(left, 0);
    const auto right_touch_count = touch_count_by_instance.value(right, 0);
    if (left_touch_count != right_touch_count) {
      return left_touch_count > right_touch_count;
    }
    const auto left_coverage =
        instance_pixel_count_in_map(instance_data, width, height, left);
    const auto right_coverage =
        instance_pixel_count_in_map(instance_data, width, height, right);
    if (left_coverage != right_coverage) {
      return left_coverage > right_coverage;
    }
    const auto left_first_seen = first_seen_by_instance.value(left, std::numeric_limits<int>::max());
    const auto right_first_seen = first_seen_by_instance.value(right, std::numeric_limits<int>::max());
    if (left_first_seen != right_first_seen) {
      return left_first_seen < right_first_seen;
    }
    return left < right;
  });
  return candidates;
}

QVariantMap segmentation_mask_for_instances(
    const QByteArray& instance_data,
    int width,
    int height,
    const QVector<int>& instance_ids) {
  if (instance_data.isEmpty() || width <= 0 || height <= 0 || instance_ids.isEmpty()) {
    return {};
  }

  QSet<int> normalized_ids;
  for (const auto instance_id : instance_ids) {
    if (instance_id > 0) {
      normalized_ids.insert(instance_id);
    }
  }
  if (normalized_ids.isEmpty()) {
    return {};
  }

  QByteArray mask_data(width * height, '\0');
  auto has_pixels = false;
  for (int index = 0; index < instance_data.size() && index < mask_data.size(); ++index) {
    if (normalized_ids.contains(static_cast<int>(
            static_cast<unsigned char>(instance_data.at(index))))) {
      mask_data[index] = static_cast<char>(0xff);
      has_pixels = true;
    }
  }

  if (!has_pixels) {
    return {};
  }

  QVariantMap mask;
  mask.insert(QStringLiteral("kind"), QStringLiteral("object"));
  mask.insert(QStringLiteral("segmentationData"), mask_data);
  mask.insert(QStringLiteral("segmentationWidth"), width);
  mask.insert(QStringLiteral("segmentationHeight"), height);
  return mask;
}

QVector<int> single_smart_select_candidate_instance_ids(
    const QVector<int>& candidate_instance_ids,
    int candidate_index) {
  if (candidate_instance_ids.isEmpty()) {
    return {};
  }

  const auto bounded_index =
      qBound(0, candidate_index, static_cast<int>(candidate_instance_ids.size()) - 1);
  return {candidate_instance_ids.at(bounded_index)};
}

double polygon_cross(const QPointF& origin, const QPointF& left, const QPointF& right) {
  return (left.x() - origin.x()) * (right.y() - origin.y()) -
         (left.y() - origin.y()) * (right.x() - origin.x());
}

quint64 grid_key(int x, int y) {
  return (static_cast<quint64>(static_cast<quint32>(x)) << 32) |
         static_cast<quint32>(y);
}

QPoint grid_point_from_key(quint64 key) {
  return QPoint(
      static_cast<int>(static_cast<quint32>(key >> 32)),
      static_cast<int>(static_cast<quint32>(key & 0xffffffffULL)));
}

quint64 directed_edge_key(quint64 start, quint64 end) {
  return start ^ (end + 0x9e3779b97f4a7c15ULL + (start << 6) + (start >> 2));
}

int cardinal_direction(quint64 start, quint64 end) {
  const auto from = grid_point_from_key(start);
  const auto to = grid_point_from_key(end);
  const auto dx = to.x() - from.x();
  const auto dy = to.y() - from.y();
  if (dx > 0) {
    return 0;
  }
  if (dy > 0) {
    return 1;
  }
  if (dx < 0) {
    return 2;
  }
  return 3;
}

int turn_priority(int previous_direction, int next_direction) {
  const auto delta = (next_direction - previous_direction + 4) % 4;
  if (delta == 1) {
    return 0;
  }
  if (delta == 0) {
    return 1;
  }
  if (delta == 3) {
    return 2;
  }
  return 3;
}

void append_directed_boundary_edge(
    int start_x,
    int start_y,
    int end_x,
    int end_y,
    QHash<quint64, QVector<quint64>>* adjacency) {
  if (adjacency == nullptr) {
    return;
  }
  adjacency->operator[](grid_key(start_x, start_y)).append(grid_key(end_x, end_y));
}

QVariantList hull_points_for_segmentation_mask(const QVariantMap& mask) {
  const auto data = mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto width = mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto height = mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (data.isEmpty() || width <= 0 || height <= 0) {
    return {};
  }

  const auto stride = std::max(1, std::max(width, height) / 96);
  QVector<QPointF> points;
  points.reserve((width / stride + 1) * (height / stride + 1));
  const auto sample_at = [&](int x, int y) {
    const auto clamped_x = qBound(0, x, width - 1);
    const auto clamped_y = qBound(0, y, height - 1);
    return static_cast<unsigned char>(data.at(clamped_y * width + clamped_x));
  };

  for (int y = 0; y < height; y += stride) {
    for (int x = 0; x < width; x += stride) {
      if (sample_at(x, y) < 96) {
        continue;
      }
      const auto boundary =
          sample_at(x - stride, y) < 96 || sample_at(x + stride, y) < 96 ||
          sample_at(x, y - stride) < 96 || sample_at(x, y + stride) < 96;
      if (!boundary) {
        continue;
      }
      points.append(QPointF(
          width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5,
          height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5));
    }
  }

  if (points.size() < 3) {
    QVariantList fallback;
    for (const auto& point : points) {
      QVariantMap point_map;
      point_map.insert(QStringLiteral("x"), clamp_unit(point.x()));
      point_map.insert(QStringLiteral("y"), clamp_unit(point.y()));
      fallback.append(point_map);
    }
    return fallback;
  }

  std::sort(points.begin(), points.end(), [](const QPointF& left, const QPointF& right) {
    if (!qFuzzyCompare(left.x() + 1.0, right.x() + 1.0)) {
      return left.x() < right.x();
    }
    return left.y() < right.y();
  });

  QVector<QPointF> hull;
  hull.reserve(points.size() * 2);
  for (const auto& point : points) {
    while (hull.size() >= 2 &&
           polygon_cross(hull[hull.size() - 2], hull[hull.size() - 1], point) <= 0.0) {
      hull.removeLast();
    }
    hull.append(point);
  }
  const auto lower_size = hull.size();
  for (qsizetype index = points.size() - 2; index >= 0; --index) {
    const auto& point = points[index];
    while (hull.size() > lower_size &&
           polygon_cross(hull[hull.size() - 2], hull[hull.size() - 1], point) <= 0.0) {
      hull.removeLast();
    }
    hull.append(point);
    if (index == 0) {
      break;
    }
  }
  if (!hull.isEmpty()) {
    hull.removeLast();
  }

  QVariantList outline;
  for (const auto& point : hull) {
    QVariantMap point_map;
    point_map.insert(QStringLiteral("x"), clamp_unit(point.x()));
    point_map.insert(QStringLiteral("y"), clamp_unit(point.y()));
    outline.append(point_map);
  }
  return outline;
}

QVariantList bounding_box_points_for_segmentation_mask(const QVariantMap& mask) {
  const auto data = mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto width = mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto height = mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (data.isEmpty() || width <= 0 || height <= 0) {
    return {};
  }

  auto min_x = width;
  auto min_y = height;
  auto max_x = -1;
  auto max_y = -1;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (static_cast<unsigned char>(data.at(y * width + x)) < 96) {
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

  const auto normalized_x = [width](int value) {
    return clamp_unit(
        width > 1 ? static_cast<double>(value) / static_cast<double>(width - 1) : 0.5);
  };
  const auto normalized_y = [height](int value) {
    return clamp_unit(
        height > 1 ? static_cast<double>(value) / static_cast<double>(height - 1) : 0.5);
  };

  QVariantList outline;
  outline.reserve(4);
  const std::array<QPoint, 4> corners{
      QPoint(min_x, min_y),
      QPoint(max_x, min_y),
      QPoint(max_x, max_y),
      QPoint(min_x, max_y),
  };
  for (const auto& corner : corners) {
    QVariantMap point_map;
    point_map.insert(QStringLiteral("x"), normalized_x(corner.x()));
    point_map.insert(QStringLiteral("y"), normalized_y(corner.y()));
    outline.append(point_map);
  }
  return outline;
}

QVariantList traced_outline_paths_for_alpha_mask(
    const QByteArray& data,
    int width,
    int height) {
  if (data.isEmpty() || width <= 1 || height <= 1) {
    return {};
  }

  const auto sample_at = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
      return static_cast<unsigned char>(0);
    }
    return static_cast<unsigned char>(data.at(y * width + x));
  };

  QHash<quint64, QVector<quint64>> adjacency;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (sample_at(x, y) < 96) {
        continue;
      }
      if (sample_at(x, y - 1) < 96) {
        append_directed_boundary_edge(x, y, x + 1, y, &adjacency);
      }
      if (sample_at(x + 1, y) < 96) {
        append_directed_boundary_edge(x + 1, y, x + 1, y + 1, &adjacency);
      }
      if (sample_at(x, y + 1) < 96) {
        append_directed_boundary_edge(x + 1, y + 1, x, y + 1, &adjacency);
      }
      if (sample_at(x - 1, y) < 96) {
        append_directed_boundary_edge(x, y + 1, x, y, &adjacency);
      }
    }
  }

  if (adjacency.isEmpty()) {
    return {};
  }

  QSet<quint64> visited_edges;
  QVector<QVector<QPoint>> loops;
  const auto starts = adjacency.keys();
  for (const auto start : starts) {
    const auto outgoing = adjacency.value(start);
    for (const auto initial_end : outgoing) {
      const auto initial_edge = directed_edge_key(start, initial_end);
      if (visited_edges.contains(initial_edge)) {
        continue;
      }

      QVector<QPoint> loop;
      auto current_start = start;
      auto current_end = initial_end;
      auto current_direction = cardinal_direction(current_start, current_end);

      while (true) {
        const auto edge = directed_edge_key(current_start, current_end);
        if (visited_edges.contains(edge)) {
          break;
        }
        visited_edges.insert(edge);
        if (loop.isEmpty()) {
          loop.append(grid_point_from_key(current_start));
        }
        loop.append(grid_point_from_key(current_end));
        if (current_end == start) {
          break;
        }

        const auto candidates = adjacency.value(current_end);
        quint64 next_end = 0;
        auto found_next = false;
        auto best_priority = std::numeric_limits<int>::max();
        for (const auto candidate : candidates) {
          if (candidate == current_start) {
            continue;
          }
          const auto candidate_edge = directed_edge_key(current_end, candidate);
          if (visited_edges.contains(candidate_edge)) {
            continue;
          }
          const auto priority =
              turn_priority(current_direction, cardinal_direction(current_end, candidate));
          if (priority < best_priority) {
            best_priority = priority;
            next_end = candidate;
            found_next = true;
          }
        }
        if (!found_next) {
          break;
        }

        current_start = current_end;
        current_end = next_end;
        current_direction = cardinal_direction(current_start, current_end);
      }

      if (loop.size() >= 4 && loop.first() == loop.last()) {
        loops.append(std::move(loop));
      }
    }
  }

  if (loops.isEmpty()) {
    return {};
  }

  std::sort(loops.begin(), loops.end(), [](const QVector<QPoint>& left, const QVector<QPoint>& right) {
    return left.size() > right.size();
  });

  QVariantList outline_paths;
  for (auto loop : loops) {
    if (loop.size() < 4) {
      continue;
    }

    loop.removeLast();
    QVector<QPoint> simplified;
    simplified.reserve(loop.size());
    for (int index = 0; index < loop.size(); ++index) {
      const auto& previous = loop[(index + loop.size() - 1) % loop.size()];
      const auto& current = loop[index];
      const auto& next = loop[(index + 1) % loop.size()];
      const auto collinear =
          (previous.x() == current.x() && current.x() == next.x()) ||
          (previous.y() == current.y() && current.y() == next.y());
      if (!collinear) {
        simplified.append(current);
      }
    }

    if (simplified.size() < 3) {
      continue;
    }

    const auto step = std::max(1, static_cast<int>(simplified.size() / 320));
    QVariantList outline;
    for (int index = 0; index < simplified.size(); index += step) {
      const auto& point = simplified[index];
      QVariantMap point_map;
      point_map.insert(
          QStringLiteral("x"),
          clamp_unit(static_cast<double>(point.x()) / static_cast<double>(width)));
      point_map.insert(
          QStringLiteral("y"),
          clamp_unit(static_cast<double>(point.y()) / static_cast<double>(height)));
      outline.append(point_map);
    }

    if (outline.size() >= 4) {
      outline_paths.append(outline);
    }
  }

  return outline_paths;
}

QVariantList outline_paths_for_mask(const QVariantMap& mask) {
  const auto kind = normalized_mask_kind(mask.value(QStringLiteral("kind")).toString());
  if (kind == QStringLiteral("polygon") || kind == QStringLiteral("lasso")) {
    QVariantList outline_paths;
    outline_paths.append(mask.value(QStringLiteral("polygonPoints")).toList());
    return outline_paths;
  }
  const auto segmentation_data = mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto segmentation_width = mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto segmentation_height = mask.value(QStringLiteral("segmentationHeight")).toInt();
  if ((is_raster_mask_kind(kind) || kind == QStringLiteral("brush")) &&
      valid_segmentation_payload(segmentation_data, segmentation_width, segmentation_height)) {
    const auto traced_outlines =
        traced_outline_paths_for_alpha_mask(segmentation_data, segmentation_width, segmentation_height);
    if (!traced_outlines.isEmpty()) {
      return traced_outlines;
    }
    const auto hull_outline = hull_points_for_segmentation_mask(mask);
    if (hull_outline.size() >= 3) {
      QVariantList outline_paths;
      outline_paths.append(hull_outline);
      return outline_paths;
    }
    QVariantList outline_paths;
    outline_paths.append(bounding_box_points_for_segmentation_mask(mask));
    return outline_paths;
  }
  return {};
}

QVariantList outline_points_for_mask(const QVariantMap& mask) {
  const auto outline_paths = outline_paths_for_mask(mask);
  if (outline_paths.isEmpty()) {
    return {};
  }
  const auto first_path = outline_paths.first().toList();
  if (!first_path.isEmpty()) {
    return first_path;
  }
  const auto first_value = outline_paths.first();
  if (first_value.metaType().id() == QMetaType::QVariantMap) {
    return outline_paths;
  }
  return {};
}

double wrap_unit(double value) {
  value -= std::floor(value);
  if (value < 0.0) {
    value += 1.0;
  }
  return value;
}

double circular_weight(double value, double center, double radius) {
  if (radius <= 0.0) {
    return 0.0;
  }

  auto distance = std::abs(value - center);
  distance = std::min(distance, 1.0 - distance);
  if (distance >= radius) {
    return 0.0;
  }

  const auto t = 1.0 - distance / radius;
  return t * t * (3.0 - 2.0 * t);
}

double preview_luminance_for_pixel(const photo_editor::pipeline::ColorPixel& pixel) {
  return (pixel.red * 0.2126) + (pixel.green * 0.7152) + (pixel.blue * 0.0722);
}

void hue_saturation_value_for_preview_pixel(
    const photo_editor::pipeline::ColorPixel& pixel,
    double* hue,
    double* saturation,
    double* value) {
  const auto max_channel = std::max({pixel.red, pixel.green, pixel.blue});
  const auto min_channel = std::min({pixel.red, pixel.green, pixel.blue});
  const auto delta = max_channel - min_channel;

  if (value != nullptr) {
    *value = clamp_unit(max_channel);
  }
  if (saturation != nullptr) {
    *saturation = max_channel <= 1e-6 ? 0.0 : clamp_unit(delta / max_channel);
  }
  if (hue == nullptr) {
    return;
  }

  if (delta <= 1e-6) {
    *hue = 0.0;
    return;
  }

  auto result = 0.0;
  if (max_channel == pixel.red) {
    result = std::fmod((pixel.green - pixel.blue) / delta, 6.0);
  } else if (max_channel == pixel.green) {
    result = ((pixel.blue - pixel.red) / delta) + 2.0;
  } else {
    result = ((pixel.red - pixel.green) / delta) + 4.0;
  }

  result /= 6.0;
  if (result < 0.0) {
    result += 1.0;
  }
  *hue = wrap_unit(result);
}

std::array<double, 3> preview_white_balance_multipliers(double temperature, double tint) {
  const auto temperature_to_kelvin = [](double slider_value) {
    const auto clamped = clamp_signed_percent(slider_value);
    if (clamped >= 0.0) {
      return std::lerp(5500.0, 2000.0, clamped / 100.0);
    }
    return std::lerp(5500.0, 12000.0, -clamped / 100.0);
  };

  const auto chromaticity_from_kelvin = [](double kelvin) {
    const auto safe_kelvin = std::clamp(kelvin, 2000.0, 12000.0);
    double x = 0.3127;
    double y = 0.3290;
    if (safe_kelvin < 4000.0) {
      x = -0.2661239e9 / std::pow(safe_kelvin, 3.0) -
          0.2343589e6 / std::pow(safe_kelvin, 2.0) +
          0.8776956e3 / safe_kelvin + 0.179910;
      if (safe_kelvin < 2222.0) {
        y = -1.1063814 * x * x * x - 1.34811020 * x * x + 2.18555832 * x - 0.20219683;
      } else {
        y = -0.9549476 * x * x * x - 1.37418593 * x * x + 2.09137015 * x - 0.16748867;
      }
    } else {
      if (safe_kelvin <= 7000.0) {
        x = -4.6070e9 / std::pow(safe_kelvin, 3.0) +
            2.9678e6 / std::pow(safe_kelvin, 2.0) +
            0.09911e3 / safe_kelvin + 0.244063;
      } else {
        x = -2.0064e9 / std::pow(safe_kelvin, 3.0) +
            1.9018e6 / std::pow(safe_kelvin, 2.0) +
            0.24748e3 / safe_kelvin + 0.237040;
      }
      y = -3.0 * x * x + 2.87 * x - 0.275;
    }
    return std::array<double, 2>{x, y};
  };

  const auto white_rgb_from_kelvin = [&](double kelvin) {
    const auto chromaticity = chromaticity_from_kelvin(kelvin);
    const auto x = chromaticity[0];
    const auto y = std::max(1e-6, chromaticity[1]);
    const auto X = x / y;
    const auto Y = 1.0;
    const auto Z = (1.0 - x - y) / y;
    return std::array<double, 3>{
        std::max(1e-6, 3.2406 * X - 1.5372 * Y - 0.4986 * Z),
        std::max(1e-6, -0.9689 * X + 1.8758 * Y + 0.0415 * Z),
        std::max(1e-6, 0.0557 * X - 0.2040 * Y + 1.0570 * Z),
    };
  };

  const auto reference_white = white_rgb_from_kelvin(5500.0);
  const auto target_white = white_rgb_from_kelvin(temperature_to_kelvin(temperature));
  const auto normalized_tint = clamp_signed_percent(tint) / 100.0;
  auto red = (target_white[0] / reference_white[0]) * std::pow(2.0, normalized_tint * 0.08);
  auto green = (target_white[1] / reference_white[1]) * std::pow(2.0, -normalized_tint * 0.24);
  auto blue = (target_white[2] / reference_white[2]) * std::pow(2.0, normalized_tint * 0.08);
  const auto normalization = (red * 0.2126) + (green * 0.7152) + (blue * 0.0722);
  if (normalization <= 1e-6) {
    return {1.0, 1.0, 1.0};
  }

  return {red / normalization, green / normalization, blue / normalization};
}

double apply_selection_expand_to_preview_weight(
    double base_weight,
    const photo_editor::pipeline::LocalMask& mask) {
  const auto expand = clamp_signed_percent(mask.selection_expand) / 100.0;
  if (std::abs(expand) <= 1e-6) {
    return clamp_unit(base_weight);
  }

  const auto threshold = std::clamp(0.5 - expand * 0.35, 0.08, 0.92);
  const auto ramp = std::clamp(0.06 + std::abs(expand) * 0.18, 0.04, 0.28);
  return smoothstep(threshold - ramp, threshold + ramp, clamp_unit(base_weight));
}

double apply_selection_edge_contrast_to_preview_weight(
    double base_weight,
    const photo_editor::pipeline::LocalMask& mask) {
  const auto contrast =
      clamp_unit(mask.selection_edge_contrast / 100.0);
  if (contrast <= 1e-6) {
    return clamp_unit(base_weight);
  }

  const auto ramp = std::lerp(0.24, 0.035, contrast);
  return smoothstep(0.5 - ramp, 0.5 + ramp, clamp_unit(base_weight));
}

double apply_selection_smooth_to_preview_weight(
    double base_weight,
    const photo_editor::pipeline::LocalMask& mask) {
  const auto smoothing = clamp_unit(mask.selection_smooth / 100.0);
  const auto weight = clamp_unit(base_weight);
  if (smoothing <= 1e-6 || weight <= 1e-6 || weight >= 1.0 - 1e-6) {
    return weight;
  }

  return std::lerp(weight, 0.5, smoothing * 0.35);
}

double apply_selection_fine_edge_to_preview_weight(
    double base_weight,
    const photo_editor::pipeline::LocalMask& mask) {
  const auto refinement = clamp_unit(mask.selection_fine_edge / 100.0);
  const auto weight = clamp_unit(base_weight);
  if (refinement <= 1e-6 || weight <= 1e-6 || weight >= 1.0 - 1e-6) {
    return weight;
  }

  const auto edge_band = 1.0 - std::abs((weight * 2.0) - 1.0);
  const auto strand_lift = std::sqrt(weight);
  const auto lifted = std::lerp(weight, strand_lift, refinement * 0.55 * edge_band);
  const auto edge_softened = std::lerp(lifted, 0.5, refinement * 0.12 * edge_band);
  return clamp_unit(edge_softened);
}

double apply_selection_refine_to_preview_weight(
    double base_weight,
    const photo_editor::pipeline::LocalMask& mask) {
  auto refined = apply_selection_expand_to_preview_weight(base_weight, mask);
  refined = apply_selection_smooth_to_preview_weight(refined, mask);
  refined = apply_selection_fine_edge_to_preview_weight(refined, mask);
  refined = apply_selection_edge_contrast_to_preview_weight(refined, mask);
  if (mask.selection_invert) {
    refined = 1.0 - refined;
  }
  return clamp_unit(refined);
}

double depth_refine_value_for_preview(
    double normalized_x,
    double normalized_y,
    const photo_editor::pipeline::ColorPixel& pixel,
    const photo_editor::pipeline::LocalMask& mask) {
  auto value = clamp_unit(preview_luminance_for_pixel(pixel));
  const auto expected_map_size =
      static_cast<std::size_t>(mask.depth_width) * static_cast<std::size_t>(mask.depth_height);
  if (mask.depth_map.empty() || mask.depth_width <= 0 || mask.depth_height <= 0 ||
      mask.depth_map.size() != expected_map_size) {
    return value;
  }

  const auto max_x = std::max(mask.depth_width - 1, 0);
  const auto max_y = std::max(mask.depth_height - 1, 0);
  const auto fx = clamp_unit(normalized_x) * static_cast<double>(max_x);
  const auto fy = clamp_unit(normalized_y) * static_cast<double>(max_y);
  const auto ix0 = std::clamp(static_cast<int>(fx), 0, max_x);
  const auto iy0 = std::clamp(static_cast<int>(fy), 0, max_y);
  const auto ix1 = std::min(ix0 + 1, max_x);
  const auto iy1 = std::min(iy0 + 1, max_y);
  const auto dx = mask.depth_width > 1 ? fx - static_cast<double>(ix0) : 0.0;
  const auto dy = mask.depth_height > 1 ? fy - static_cast<double>(iy0) : 0.0;
  const auto idx = [&](int x, int y) {
    return static_cast<std::size_t>(y * mask.depth_width + x);
  };
  const auto sample = [&](int x, int y) {
    return static_cast<double>(mask.depth_map[idx(x, y)]) / 255.0;
  };
  const auto top = sample(ix0, iy0) * (1.0 - dx) + sample(ix1, iy0) * dx;
  const auto bottom = sample(ix0, iy1) * (1.0 - dx) + sample(ix1, iy1) * dx;
  return clamp_unit(top * (1.0 - dy) + bottom * dy);
}

double unit_range_refine_weight(double value, double min_value, double max_value, double feather) {
  auto lower = std::clamp(min_value, 0.0, 1.0);
  auto upper = std::clamp(max_value, 0.0, 1.0);
  if (lower > upper) {
    std::swap(lower, upper);
  }
  const auto edge = std::clamp(feather, 0.0, 0.5);
  const auto low_weight = lower <= 1.0e-9
                              ? 1.0
                              : smoothstep(std::max(0.0, lower - edge), lower, clamp_unit(value));
  const auto high_weight = upper >= 1.0 - 1.0e-9
                               ? 1.0
                               : 1.0 - smoothstep(upper, std::min(1.0, upper + edge), clamp_unit(value));
  return clamp_unit(low_weight * high_weight);
}

double apply_range_refinement_to_preview_weight(
    double base_weight,
    double normalized_x,
    double normalized_y,
    const photo_editor::pipeline::ColorPixel& pixel,
    const photo_editor::pipeline::LocalMask& mask) {
  auto weight = clamp_unit(base_weight);
  if (weight <= 1.0e-9) {
    return 0.0;
  }

  const auto feather = std::clamp(mask.refine_range_feather, 0.0, 0.5);
  weight *= unit_range_refine_weight(
      preview_luminance_for_pixel(pixel),
      mask.refine_luminance_min,
      mask.refine_luminance_max,
      feather);

  double hue = 0.0;
  double saturation = 0.0;
  hue_saturation_value_for_preview_pixel(pixel, &hue, &saturation, nullptr);
  weight *= circular_weight(hue, wrap_unit(mask.refine_hue_center), std::max(0.02, mask.refine_hue_range));
  weight *= unit_range_refine_weight(
      saturation,
      mask.refine_saturation_min,
      mask.refine_saturation_max,
      feather);
  weight *= unit_range_refine_weight(
      depth_refine_value_for_preview(normalized_x, normalized_y, pixel, mask),
      mask.refine_depth_min,
      mask.refine_depth_max,
      feather);

  const auto edge_min = std::clamp(mask.refine_edge_confidence_min, 0.0, 1.0);
  if (edge_min > 1.0e-9) {
    const auto edge_confidence = clamp_unit(std::abs(clamp_unit(base_weight) - 0.5) * 2.0);
    weight *= smoothstep(edge_min, 1.0, edge_confidence);
  }
  return clamp_unit(weight);
}

double apply_full_refine_to_preview_weight(
    double base_weight,
    double normalized_x,
    double normalized_y,
    const photo_editor::pipeline::ColorPixel& pixel,
    const photo_editor::pipeline::LocalMask& mask) {
  return apply_range_refinement_to_preview_weight(
      apply_selection_refine_to_preview_weight(base_weight, mask),
      normalized_x,
      normalized_y,
      pixel,
      mask);
}

enum class MaskPreviewMode {
  Overlay,
  Grayscale,
  BlackOnWhite,
  WhiteOnBlack,
  MarchingAnts,
  Edge,
  AffectedPixels,
};

MaskPreviewMode mask_preview_mode_from_string(const QString& value) {
  const auto normalized = value.trimmed().toLower();
  if (normalized == QStringLiteral("grayscale") || normalized == QStringLiteral("gray")) {
    return MaskPreviewMode::Grayscale;
  }
  if (normalized == QStringLiteral("black-on-white") || normalized == QStringLiteral("black on white")) {
    return MaskPreviewMode::BlackOnWhite;
  }
  if (normalized == QStringLiteral("white-on-black") || normalized == QStringLiteral("white on black")) {
    return MaskPreviewMode::WhiteOnBlack;
  }
  if (normalized == QStringLiteral("marching-ants") || normalized == QStringLiteral("marching ants")) {
    return MaskPreviewMode::MarchingAnts;
  }
  if (normalized == QStringLiteral("edge") || normalized == QStringLiteral("edge-view") ||
      normalized == QStringLiteral("edge view")) {
    return MaskPreviewMode::Edge;
  }
  if (normalized == QStringLiteral("affected") || normalized == QStringLiteral("affected-pixels") ||
      normalized == QStringLiteral("affected pixels")) {
    return MaskPreviewMode::AffectedPixels;
  }
  return MaskPreviewMode::Overlay;
}

QString mask_preview_mode_to_string(MaskPreviewMode mode) {
  switch (mode) {
    case MaskPreviewMode::Grayscale:
      return QStringLiteral("grayscale");
    case MaskPreviewMode::BlackOnWhite:
      return QStringLiteral("black-on-white");
    case MaskPreviewMode::WhiteOnBlack:
      return QStringLiteral("white-on-black");
    case MaskPreviewMode::MarchingAnts:
      return QStringLiteral("marching-ants");
    case MaskPreviewMode::Edge:
      return QStringLiteral("edge");
    case MaskPreviewMode::AffectedPixels:
      return QStringLiteral("affected");
    case MaskPreviewMode::Overlay:
    default:
      return QStringLiteral("overlay");
  }
}

QColor preview_background_color_for_mode(MaskPreviewMode mode) {
  switch (mode) {
    case MaskPreviewMode::BlackOnWhite:
      return QColor(255, 255, 255, 230);
    case MaskPreviewMode::WhiteOnBlack:
      return QColor(10, 14, 18, 230);
    case MaskPreviewMode::MarchingAnts:
    case MaskPreviewMode::Edge:
    case MaskPreviewMode::AffectedPixels:
    case MaskPreviewMode::Overlay:
    case MaskPreviewMode::Grayscale:
    default:
      return QColor(0, 0, 0, 0);
  }
}

QColor preview_fill_color_for_mode(const QVariantMap& mask, MaskPreviewMode mode) {
  switch (mode) {
    case MaskPreviewMode::Grayscale:
      return QColor(QStringLiteral("#edf1f4"));
    case MaskPreviewMode::BlackOnWhite:
      return QColor(QStringLiteral("#101419"));
    case MaskPreviewMode::WhiteOnBlack:
      return QColor(QStringLiteral("#f4f7fa"));
    case MaskPreviewMode::MarchingAnts:
    case MaskPreviewMode::Edge:
      return QColor(QStringLiteral("#f6f3ed"));
    case MaskPreviewMode::AffectedPixels:
      return QColor(QStringLiteral("#f0c987"));
    case MaskPreviewMode::Overlay:
    default:
      Q_UNUSED(mask);
      return QColor(QStringLiteral("#cf3b32"));
  }
}

int preview_fill_alpha_for_mode(
    double weight,
    MaskPreviewMode mode,
    double alpha_scale,
    const QVariantMap& mask) {
  const auto clamped_weight = clamp_unit(weight);
  const auto tool_role =
      normalized_mask_tool_role(mask.value(QStringLiteral("toolRole")).toString());
  const auto retouch_mode =
      mask.value(QStringLiteral("retouchMode")).toString().trimmed().toLower();
  const auto operation =
      normalized_mask_operation(mask.value(QStringLiteral("operation")).toString());
  switch (mode) {
    case MaskPreviewMode::Grayscale:
      return qBound(0, static_cast<int>(std::lround(clamped_weight * 255.0 * 0.92)), 255);
    case MaskPreviewMode::BlackOnWhite:
    case MaskPreviewMode::WhiteOnBlack:
      return qBound(0, static_cast<int>(std::lround(clamped_weight * 255.0)), 255);
    case MaskPreviewMode::MarchingAnts:
      return 0;
    case MaskPreviewMode::Edge: {
      const auto edge_weight = 1.0 - std::abs(clamped_weight - 0.5) * 2.0;
      return qBound(0, static_cast<int>(std::lround(clamp_unit(edge_weight) * 255.0)), 255);
    }
    case MaskPreviewMode::AffectedPixels:
      return qBound(
          0,
          static_cast<int>(std::lround(
              smoothstep(0.04, 0.18, clamped_weight) * 255.0 * 0.72)),
          255);
    case MaskPreviewMode::Overlay:
    default:
      Q_UNUSED(tool_role);
      Q_UNUSED(retouch_mode);
      Q_UNUSED(operation);
      return qBound(
          0,
          static_cast<int>(std::lround(clamped_weight * 255.0 *
                                       std::min(clamp_unit(alpha_scale), 0.34))),
          255);
  }
}

QString encode_preview_png_source(const QImage& image) {
  QByteArray encoded;
  QBuffer buffer(&encoded);
  if (!buffer.open(QIODevice::WriteOnly)) {
    return {};
  }

  QImageWriter writer(&buffer, "PNG");
  writer.setCompression(1);
  if (!writer.write(image)) {
    return {};
  }

  return QStringLiteral("data:image/png;base64,%1")
      .arg(QString::fromLatin1(encoded.toBase64()));
}

QByteArray encode_png_bytes(const QImage& image) {
  if (image.isNull()) {
    return {};
  }

  QByteArray encoded;
  QBuffer buffer(&encoded);
  if (!buffer.open(QIODevice::WriteOnly)) {
    return {};
  }

  QImageWriter writer(&buffer, "PNG");
  writer.setCompression(1);
  if (!writer.write(image)) {
    return {};
  }
  return encoded;
}

QString segmentation_mask_preview_source(
    const QVariantMap& mask,
    const QString& preview_mode,
    double alpha_scale) {
  const auto data = mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto width = mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto height = mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (!valid_segmentation_payload(data, width, height)) {
    return {};
  }

  const auto mode = mask_preview_mode_from_string(preview_mode);
  QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
  image.fill(preview_background_color_for_mode(mode));

  const auto color = preview_fill_color_for_mode(mask, mode);
  for (int y = 0; y < height; ++y) {
    auto* scanline = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = 0; x < width; ++x) {
      const auto index = y * width + x;
      const auto weight =
          static_cast<double>(static_cast<unsigned char>(data.at(index))) / 255.0;
      const auto alpha = preview_fill_alpha_for_mode(weight, mode, alpha_scale, mask);
      scanline[x] = qRgba(color.red(), color.green(), color.blue(), qBound(0, alpha, 255));
    }
  }

  return encode_preview_png_source(image);
}

QVariantMap curve_point(double x, double y) {
  QVariantMap point;
  point.insert(QStringLiteral("x"), clamp_unit(x));
  point.insert(QStringLiteral("y"), clamp_unit(y));
  return point;
}

double curve_point_coordinate(const QVariant& value, const QString& key, double fallback) {
  const auto point = value.toMap();
  if (!point.contains(key)) {
    return clamp_unit(fallback);
  }
  return clamp_unit(point.value(key).toDouble());
}

QVariantList default_curve_points() {
  return {
      curve_point(0.0, 0.0),
      curve_point(1.0, 1.0),
  };
}

const QStringList& mixer_channel_names() {
  static const QStringList kNames = {
      QStringLiteral("Reds"), QStringLiteral("Oranges"), QStringLiteral("Yellows"),
      QStringLiteral("Greens"), QStringLiteral("Aquas"), QStringLiteral("Blues"),
      QStringLiteral("Purples"), QStringLiteral("Magentas"),
  };
  return kNames;
}

int mixer_channel_index(const QString& channel) {
  return static_cast<int>(mixer_channel_names().indexOf(channel));
}

QStringList curve_preset_names() {
  return {
      QStringLiteral("Linear"),
      QStringLiteral("Medium Contrast"),
      QStringLiteral("Strong Contrast"),
      QStringLiteral("Matte Lift"),
      QStringLiteral("Soft Fade"),
      QStringLiteral("Filmic S"),
      QStringLiteral("Shadow Lift"),
      QStringLiteral("Highlight Roll-off"),
  };
}

QVariantList curve_preset_points(const QString& name) {
  const auto normalized = name.trimmed().toLower();
  if (normalized == QStringLiteral("linear")) {
    return default_curve_points();
  }
  if (normalized == QStringLiteral("medium contrast")) {
    return {
        curve_point(0.0, 0.0),
        curve_point(0.22, 0.16),
        curve_point(0.50, 0.50),
        curve_point(0.78, 0.84),
        curve_point(1.0, 1.0),
    };
  }
  if (normalized == QStringLiteral("strong contrast")) {
    return {
        curve_point(0.0, 0.0),
        curve_point(0.18, 0.10),
        curve_point(0.50, 0.50),
        curve_point(0.82, 0.90),
        curve_point(1.0, 1.0),
    };
  }
  if (normalized == QStringLiteral("matte lift")) {
    return {
        curve_point(0.0, 0.08),
        curve_point(0.25, 0.22),
        curve_point(0.55, 0.57),
        curve_point(0.82, 0.86),
        curve_point(1.0, 0.97),
    };
  }
  if (normalized == QStringLiteral("soft fade")) {
    return {
        curve_point(0.0, 0.05),
        curve_point(0.30, 0.28),
        curve_point(0.70, 0.72),
        curve_point(1.0, 0.96),
    };
  }
  if (normalized == QStringLiteral("filmic s")) {
    return {
        curve_point(0.0, 0.02),
        curve_point(0.18, 0.12),
        curve_point(0.45, 0.46),
        curve_point(0.74, 0.82),
        curve_point(1.0, 0.98),
    };
  }
  if (normalized == QStringLiteral("shadow lift")) {
    return {
        curve_point(0.0, 0.06),
        curve_point(0.25, 0.31),
        curve_point(0.55, 0.58),
        curve_point(1.0, 1.0),
    };
  }
  if (normalized == QStringLiteral("highlight roll-off") ||
      normalized == QStringLiteral("highlight rolloff")) {
    return {
        curve_point(0.0, 0.0),
        curve_point(0.45, 0.47),
        curve_point(0.75, 0.78),
        curve_point(1.0, 0.92),
    };
  }
  return {};
}

struct BuiltInPresetDefinition final {
  QString name;
  QString pack;
  QString category;
  QStringList adjustment_groups;
  QJsonArray adaptive_rules;
  EditRecipeState state;
};

QJsonObject adaptive_rule(
    const QString& source,
    const QString& contains,
    const QString& field,
    double delta) {
  QJsonObject rule;
  rule.insert(QStringLiteral("source"), source);
  rule.insert(QStringLiteral("contains"), contains);
  rule.insert(QStringLiteral("field"), field);
  rule.insert(QStringLiteral("delta"), delta);
  return rule;
}

QJsonObject adaptive_metric_rule(
    const QString& metric,
    int above,
    const QString& field,
    double delta) {
  QJsonObject rule;
  rule.insert(QStringLiteral("source"), QStringLiteral("image"));
  rule.insert(QStringLiteral("metric"), metric);
  rule.insert(QStringLiteral("above"), above);
  rule.insert(QStringLiteral("field"), field);
  rule.insert(QStringLiteral("delta"), delta);
  return rule;
}

QVector<BuiltInPresetDefinition> built_in_preset_definitions() {
  QVector<BuiltInPresetDefinition> presets;
  presets.reserve(10);

  EditRecipeState clean_portrait;
  clean_portrait.exposure = 0.18;
  clean_portrait.contrast = 10.0;
  clean_portrait.highlights = -18.0;
  clean_portrait.shadows = 16.0;
  clean_portrait.whites = 8.0;
  clean_portrait.blacks = -8.0;
  clean_portrait.texture = -8.0;
  clean_portrait.clarity = -6.0;
  clean_portrait.sharpening = 22.0;
  clean_portrait.sharpen_masking = 58.0;
  clean_portrait.vibrance = 8.0;
  clean_portrait.mixer_channels[0].saturation = -6.0;
  clean_portrait.mixer_channels[1].saturation = 5.0;
  clean_portrait.mixer_channels[1].luminance = 8.0;
  presets.push_back({
      QStringLiteral("Clean Portrait"),
      QStringLiteral("Portrait Essentials"),
      QStringLiteral("Portrait"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      clean_portrait,
  });

  EditRecipeState camera_aware_portrait = clean_portrait;
  camera_aware_portrait.texture = -10.0;
  camera_aware_portrait.clarity = -8.0;
  QJsonArray camera_aware_portrait_rules;
  camera_aware_portrait_rules.push_back(adaptive_rule(
      QStringLiteral("camera"),
      QStringLiteral("Canon EOS R5"),
      QStringLiteral("temperature"),
      2.0));
  camera_aware_portrait_rules.push_back(adaptive_rule(
      QStringLiteral("lens"),
      QStringLiteral("RF 28-70"),
      QStringLiteral("clarity"),
      4.0));
  camera_aware_portrait_rules.push_back(adaptive_metric_rule(
      QStringLiteral("highlightClips"),
      0,
      QStringLiteral("highlights"),
      -8.0));
  presets.push_back({
      QStringLiteral("Camera Aware Portrait"),
      QStringLiteral("Portrait Essentials"),
      QStringLiteral("Portrait"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      camera_aware_portrait_rules,
      camera_aware_portrait,
  });

  EditRecipeState soft_portrait = clean_portrait;
  soft_portrait.exposure = 0.10;
  soft_portrait.contrast = -4.0;
  soft_portrait.highlights = -24.0;
  soft_portrait.texture = -18.0;
  soft_portrait.clarity = -14.0;
  soft_portrait.curve_shadows = 10.0;
  soft_portrait.curve_highlights = -6.0;
  soft_portrait.grading_shadows_hue = 0.60;
  soft_portrait.grading_shadows_saturation = 8.0;
  soft_portrait.grading_highlights_hue = 0.09;
  soft_portrait.grading_highlights_saturation = 7.0;
  presets.push_back({
      QStringLiteral("Soft Skin Editorial"),
      QStringLiteral("Portrait Essentials"),
      QStringLiteral("Portrait"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      soft_portrait,
  });

  EditRecipeState dramatic_portrait = clean_portrait;
  dramatic_portrait.contrast = 24.0;
  dramatic_portrait.highlights = -12.0;
  dramatic_portrait.shadows = -12.0;
  dramatic_portrait.blacks = -18.0;
  dramatic_portrait.clarity = 12.0;
  dramatic_portrait.dehaze = 5.0;
  dramatic_portrait.grading_shadows_hue = 0.62;
  dramatic_portrait.grading_shadows_saturation = 10.0;
  dramatic_portrait.grading_midtones_hue = 0.08;
  dramatic_portrait.grading_midtones_saturation = 4.0;
  presets.push_back({
      QStringLiteral("Dramatic Portrait"),
      QStringLiteral("Portrait Essentials"),
      QStringLiteral("Portrait"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      dramatic_portrait,
  });

  EditRecipeState landscape_pop;
  landscape_pop.exposure = 0.08;
  landscape_pop.contrast = 18.0;
  landscape_pop.highlights = -28.0;
  landscape_pop.shadows = 18.0;
  landscape_pop.whites = 14.0;
  landscape_pop.blacks = -14.0;
  landscape_pop.texture = 18.0;
  landscape_pop.clarity = 20.0;
  landscape_pop.dehaze = 14.0;
  landscape_pop.vibrance = 22.0;
  landscape_pop.mixer_channels[3].saturation = 12.0;
  landscape_pop.mixer_channels[5].saturation = 16.0;
  landscape_pop.mixer_channels[5].luminance = -8.0;
  presets.push_back({
      QStringLiteral("Landscape Pop"),
      QStringLiteral("Landscape Core"),
      QStringLiteral("Landscape"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      landscape_pop,
  });

  EditRecipeState golden_hour = landscape_pop;
  golden_hour.temperature = 8.0;
  golden_hour.tint = 2.0;
  golden_hour.contrast = 12.0;
  golden_hour.highlights = -22.0;
  golden_hour.grading_highlights_hue = 0.10;
  golden_hour.grading_highlights_saturation = 18.0;
  golden_hour.grading_midtones_hue = 0.08;
  golden_hour.grading_midtones_saturation = 8.0;
  presets.push_back({
      QStringLiteral("Golden Hour Landscape"),
      QStringLiteral("Landscape Core"),
      QStringLiteral("Landscape"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      golden_hour,
  });

  EditRecipeState moody_forest = landscape_pop;
  moody_forest.exposure = -0.10;
  moody_forest.contrast = 20.0;
  moody_forest.shadows = -8.0;
  moody_forest.blacks = -22.0;
  moody_forest.vibrance = 8.0;
  moody_forest.mixer_channels[3].hue = -8.0;
  moody_forest.mixer_channels[3].saturation = -8.0;
  moody_forest.mixer_channels[3].luminance = -12.0;
  presets.push_back({
      QStringLiteral("Moody Forest"),
      QStringLiteral("Landscape Core"),
      QStringLiteral("Landscape"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      moody_forest,
  });

  EditRecipeState bw_clean;
  bw_clean.black_and_white_enabled = true;
  bw_clean.contrast = 16.0;
  bw_clean.highlights = -12.0;
  bw_clean.shadows = 10.0;
  bw_clean.blacks = -16.0;
  bw_clean.texture = 8.0;
  bw_clean.clarity = 10.0;
  bw_clean.black_and_white_mixer = {8.0, 16.0, 12.0, -4.0, 0.0, -12.0, -4.0, 4.0};
  presets.push_back({
      QStringLiteral("Clean Black and White"),
      QStringLiteral("Monochrome"),
      QStringLiteral("Black and White"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      bw_clean,
  });

  EditRecipeState bw_matte = bw_clean;
  bw_matte.contrast = 8.0;
  bw_matte.blacks = -4.0;
  bw_matte.curve_shadows = 18.0;
  bw_matte.curve_darks = 8.0;
  bw_matte.grading_shadows_hue = 0.11;
  bw_matte.grading_shadows_saturation = 5.0;
  presets.push_back({
      QStringLiteral("Matte Black and White"),
      QStringLiteral("Monochrome"),
      QStringLiteral("Black and White"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      bw_matte,
  });

  EditRecipeState bw_grit = bw_clean;
  bw_grit.contrast = 28.0;
  bw_grit.texture = 24.0;
  bw_grit.clarity = 24.0;
  bw_grit.dehaze = 8.0;
  bw_grit.sharpening = 34.0;
  bw_grit.black_and_white_mixer = {-8.0, 8.0, 10.0, -12.0, -6.0, -20.0, -10.0, -4.0};
  presets.push_back({
      QStringLiteral("High Contrast Grit"),
      QStringLiteral("Monochrome"),
      QStringLiteral("Black and White"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      bw_grit,
  });

  return presets;
}

QMap<QString, EditRecipeState> built_in_preset_map() {
  QMap<QString, EditRecipeState> presets;
  for (const auto& preset : built_in_preset_definitions()) {
    presets.insert(preset.name, preset.state);
  }
  return presets;
}

QMap<QString, QString> built_in_preset_pack_map() {
  QMap<QString, QString> packs;
  for (const auto& preset : built_in_preset_definitions()) {
    packs.insert(preset.name, preset.pack);
  }
  return packs;
}

QMap<QString, QString> built_in_preset_category_map() {
  QMap<QString, QString> categories;
  for (const auto& preset : built_in_preset_definitions()) {
    categories.insert(preset.name, preset.category);
  }
  return categories;
}

QMap<QString, QStringList> built_in_preset_group_map() {
  QMap<QString, QStringList> groups;
  for (const auto& preset : built_in_preset_definitions()) {
    groups.insert(preset.name, preset.adjustment_groups);
  }
  return groups;
}

QMap<QString, QJsonArray> built_in_preset_adaptive_rule_map() {
  QMap<QString, QJsonArray> rules;
  for (const auto& preset : built_in_preset_definitions()) {
    if (!preset.adaptive_rules.isEmpty()) {
      rules.insert(preset.name, preset.adaptive_rules);
    }
  }
  return rules;
}

QString normalized_preset_category(const QString& category, const QString& fallback) {
  const auto normalized = category.simplified();
  if (!normalized.isEmpty()) {
    return normalized;
  }
  const auto normalized_fallback = fallback.simplified();
  return normalized_fallback.isEmpty() ? QStringLiteral("User") : normalized_fallback;
}

QStringList normalized_preset_adjustment_groups(const QStringList& groups) {
  QStringList normalized_groups;
  for (const auto& group : groups) {
    const auto normalized = normalize_adjustment_group(group);
    if (normalized.isEmpty()) {
      continue;
    }
    if (normalized == QStringLiteral("all")) {
      return all_adjustment_groups();
    }
    if (!normalized_groups.contains(normalized)) {
      normalized_groups.push_back(normalized);
    }
  }
  normalized_groups.sort(Qt::CaseInsensitive);
  return normalized_groups;
}

QStringList preset_adjustment_groups_from_json(const QJsonArray& group_array) {
  QStringList groups;
  for (const auto& group_value : group_array) {
    groups.push_back(group_value.toString());
  }
  return normalized_preset_adjustment_groups(groups);
}

QStringList declared_preset_groups_for_state(const EditRecipeState& state) {
  QStringList groups;
  const EditRecipeState neutral_state;
  for (const auto& group : all_adjustment_groups()) {
    const auto group_state = merge_edit_recipe_state_by_groups(neutral_state, state, {group});
    if (!edit_states_equal(group_state, neutral_state)) {
      groups.push_back(group);
    }
  }
  groups.sort(Qt::CaseInsensitive);
  return groups;
}

bool preset_has_crop_safety_changes(const EditRecipeState& state) {
  return std::abs(state.crop_rect_x) > 1e-6 ||
         std::abs(state.crop_rect_y) > 1e-6 ||
         std::abs(state.crop_rect_width) > 1e-6 ||
         std::abs(state.crop_rect_height) > 1e-6 ||
         std::abs(state.crop_zoom - 1.0) > 1e-6 ||
         state.crop_preset != QStringLiteral("Original") ||
         state.upright_mode != QStringLiteral("Off") ||
         std::abs(state.upright_vertical_guide_angle) > 1e-6 ||
         std::abs(state.upright_horizontal_guide_angle) > 1e-6 ||
         std::abs(state.perspective_vertical) > 1e-6 ||
         std::abs(state.perspective_horizontal) > 1e-6 ||
         std::abs(state.transform_rotation) > 1e-6 ||
         std::abs(state.transform_scale - 1.0) > 1e-6 ||
         std::abs(state.transform_skew_x) > 1e-6 ||
         std::abs(state.transform_skew_y) > 1e-6 ||
         std::abs(state.liquify_strength) > 1e-6 ||
         state.content_aware_crop_fill;
}

bool preset_has_mask_safety_changes(const EditRecipeState& state) {
  return has_structured_linear_mask(state) || has_structured_radial_mask(state) || !state.mask_stack.isEmpty();
}

bool preset_has_output_safety_changes(const EditRecipeState& state) {
  const EditRecipeState neutral_state;
  return state.preview_color_space != neutral_state.preview_color_space ||
         state.monitor_color_profile != neutral_state.monitor_color_profile ||
         state.soft_proof_enabled != neutral_state.soft_proof_enabled ||
         state.soft_proof_profile != neutral_state.soft_proof_profile ||
         state.output_color_space != neutral_state.output_color_space ||
         std::abs(state.output_headroom - neutral_state.output_headroom) > 1e-6;
}

QStringList preset_safety_warnings_for_state(
    const EditRecipeState& state,
    const QStringList& groups) {
  const auto normalized_groups = normalize_adjustment_groups(groups);
  QStringList warnings;
  if (normalized_groups.contains(QStringLiteral("crop")) && preset_has_crop_safety_changes(state)) {
    warnings.push_back(QStringLiteral("Preset changes crop, transform, or geometry settings."));
  }
  if (normalized_groups.contains(QStringLiteral("masks")) && preset_has_mask_safety_changes(state)) {
    warnings.push_back(QStringLiteral("Preset changes masks or local adjustment regions."));
  }
  if (normalized_groups.contains(QStringLiteral("color")) && preset_has_output_safety_changes(state)) {
    warnings.push_back(QStringLiteral("Preset changes output color, proofing, or display settings."));
  }
  return warnings;
}

bool preset_values_differ(double left, double right) {
  return std::abs(left - right) > 1e-6;
}

void append_changed_control(QStringList* controls, const QString& label, bool changed) {
  if (controls == nullptr || !changed || controls->contains(label)) {
    return;
  }
  controls->push_back(label);
}

bool mixer_channels_differ(
    const std::array<EditRecipeState::MixerChannel, 8>& before,
    const std::array<EditRecipeState::MixerChannel, 8>& after) {
  for (std::size_t index = 0; index < before.size(); ++index) {
    if (preset_values_differ(before[index].hue, after[index].hue) ||
        preset_values_differ(before[index].saturation, after[index].saturation) ||
        preset_values_differ(before[index].luminance, after[index].luminance) ||
        preset_values_differ(before[index].selective_cyan, after[index].selective_cyan) ||
        preset_values_differ(before[index].selective_magenta, after[index].selective_magenta) ||
        preset_values_differ(before[index].selective_yellow, after[index].selective_yellow) ||
        preset_values_differ(before[index].selective_black, after[index].selective_black)) {
      return true;
    }
  }
  return false;
}

bool rgb_channel_mixers_differ(
    const EditRecipeState::RgbChannelMixer& before,
    const EditRecipeState::RgbChannelMixer& after) {
  return preset_values_differ(before.red_from_red, after.red_from_red) ||
         preset_values_differ(before.red_from_green, after.red_from_green) ||
         preset_values_differ(before.red_from_blue, after.red_from_blue) ||
         preset_values_differ(before.green_from_red, after.green_from_red) ||
         preset_values_differ(before.green_from_green, after.green_from_green) ||
         preset_values_differ(before.green_from_blue, after.green_from_blue) ||
         preset_values_differ(before.blue_from_red, after.blue_from_red) ||
         preset_values_differ(before.blue_from_green, after.blue_from_green) ||
         preset_values_differ(before.blue_from_blue, after.blue_from_blue);
}

bool black_and_white_mixers_differ(
    const std::array<double, 8>& before,
    const std::array<double, 8>& after) {
  for (std::size_t index = 0; index < before.size(); ++index) {
    if (preset_values_differ(before[index], after[index])) {
      return true;
    }
  }
  return false;
}

QStringList changed_controls_between_states(
    const EditRecipeState& before,
    const EditRecipeState& after) {
  QStringList controls;
  append_changed_control(&controls, QStringLiteral("Exposure"), preset_values_differ(before.exposure, after.exposure));
  append_changed_control(&controls, QStringLiteral("Contrast"), preset_values_differ(before.contrast, after.contrast));
  append_changed_control(&controls, QStringLiteral("Highlights"), preset_values_differ(before.highlights, after.highlights));
  append_changed_control(&controls, QStringLiteral("Shadows"), preset_values_differ(before.shadows, after.shadows));
  append_changed_control(&controls, QStringLiteral("Whites"), preset_values_differ(before.whites, after.whites));
  append_changed_control(&controls, QStringLiteral("Blacks"), preset_values_differ(before.blacks, after.blacks));
  append_changed_control(&controls, QStringLiteral("Temperature"), preset_values_differ(before.temperature, after.temperature));
  append_changed_control(&controls, QStringLiteral("Tint"), preset_values_differ(before.tint, after.tint));
  append_changed_control(&controls, QStringLiteral("Vibrance"), preset_values_differ(before.vibrance, after.vibrance));
  append_changed_control(&controls, QStringLiteral("Saturation"), preset_values_differ(before.saturation, after.saturation));
  append_changed_control(&controls, QStringLiteral("Texture"), preset_values_differ(before.texture, after.texture));
  append_changed_control(&controls, QStringLiteral("Clarity"), preset_values_differ(before.clarity, after.clarity));
  append_changed_control(&controls, QStringLiteral("Dehaze"), preset_values_differ(before.dehaze, after.dehaze));
  append_changed_control(&controls, QStringLiteral("Sharpening"), preset_values_differ(before.sharpening, after.sharpening));
  append_changed_control(
      &controls,
      QStringLiteral("Luminance NR"),
      preset_values_differ(before.luminance_nr, after.luminance_nr));
  append_changed_control(
      &controls,
      QStringLiteral("Color NR"),
      preset_values_differ(before.color_nr, after.color_nr));
  append_changed_control(
      &controls,
      QStringLiteral("Optics"),
      preset_values_differ(before.lens_distortion, after.lens_distortion) ||
          preset_values_differ(before.lens_vignette, after.lens_vignette) ||
          preset_values_differ(before.chromatic_aberration, after.chromatic_aberration));
  append_changed_control(
      &controls,
      QStringLiteral("Color Mixer"),
      mixer_channels_differ(before.mixer_channels, after.mixer_channels) ||
          rgb_channel_mixers_differ(before.rgb_channel_mixer, after.rgb_channel_mixer));
  append_changed_control(
      &controls,
      QStringLiteral("Black and White"),
      before.black_and_white_enabled != after.black_and_white_enabled ||
          black_and_white_mixers_differ(before.black_and_white_mixer, after.black_and_white_mixer));
  append_changed_control(
      &controls,
      QStringLiteral("Color Grading"),
      preset_values_differ(before.grading_shadows_hue, after.grading_shadows_hue) ||
          preset_values_differ(before.grading_shadows_saturation, after.grading_shadows_saturation) ||
          preset_values_differ(before.grading_midtones_hue, after.grading_midtones_hue) ||
          preset_values_differ(before.grading_midtones_saturation, after.grading_midtones_saturation) ||
          preset_values_differ(before.grading_highlights_hue, after.grading_highlights_hue) ||
          preset_values_differ(before.grading_highlights_saturation, after.grading_highlights_saturation));
  append_changed_control(&controls, QStringLiteral("Crop"), preset_has_crop_safety_changes(after) &&
                                                        !edit_states_equal(
                                                            merge_edit_recipe_state_by_groups(before, after, {QStringLiteral("crop")}),
                                                            before));
  append_changed_control(&controls, QStringLiteral("Masks"), before.mask_stack != after.mask_stack ||
                                                         before.linear_mask_enabled != after.linear_mask_enabled ||
                                                         before.radial_mask_enabled != after.radial_mask_enabled);
  append_changed_control(
      &controls,
      QStringLiteral("Output"),
      before.output_color_space != after.output_color_space ||
          before.preview_color_space != after.preview_color_space ||
          before.soft_proof_enabled != after.soft_proof_enabled ||
          before.soft_proof_profile != after.soft_proof_profile);
  return controls;
}

QJsonArray preset_adjustment_groups_to_json(const QStringList& groups) {
  QJsonArray group_array;
  for (const auto& group : normalized_preset_adjustment_groups(groups)) {
    group_array.push_back(group);
  }
  return group_array;
}

QString preset_sync_manifest_file_path(const QString& folder_path) {
  const auto normalized_folder = folder_path.trimmed();
  if (normalized_folder.isEmpty()) {
    return {};
  }
  return QDir(normalized_folder).filePath(QStringLiteral("photo-editor-presets-sync.json"));
}

bool adaptive_metadata_matches(const QString& value, const QJsonObject& rule) {
  const auto contains = rule.value(QStringLiteral("contains")).toString().simplified();
  return contains.isEmpty() || value.contains(contains, Qt::CaseInsensitive);
}

bool adaptive_metric_matches(int value, const QJsonObject& rule) {
  if (rule.contains(QStringLiteral("above")) && value <= rule.value(QStringLiteral("above")).toInt()) {
    return false;
  }
  if (rule.contains(QStringLiteral("below")) && value >= rule.value(QStringLiteral("below")).toInt()) {
    return false;
  }
  return rule.contains(QStringLiteral("above")) || rule.contains(QStringLiteral("below"));
}

bool adaptive_rule_matches(
    const QJsonObject& rule,
    const QString& camera_label,
    const QString& lens_model,
    const QString& capture_date,
    int raw_highlight_clip_count,
    int raw_shadow_clip_count) {
  const auto source = rule.value(QStringLiteral("source")).toString().trimmed().toLower();
  if (source == QStringLiteral("camera")) {
    return adaptive_metadata_matches(camera_label, rule);
  }
  if (source == QStringLiteral("lens")) {
    return adaptive_metadata_matches(lens_model, rule);
  }
  if (source == QStringLiteral("capturedate") || source == QStringLiteral("capture_date")) {
    return adaptive_metadata_matches(capture_date, rule);
  }
  if (source == QStringLiteral("image")) {
    const auto metric = rule.value(QStringLiteral("metric")).toString().trimmed();
    if (metric == QStringLiteral("highlightClips")) {
      return adaptive_metric_matches(raw_highlight_clip_count, rule);
    }
    if (metric == QStringLiteral("shadowClips")) {
      return adaptive_metric_matches(raw_shadow_clip_count, rule);
    }
  }
  return false;
}

bool apply_adaptive_delta(EditRecipeState* state, const QString& field, double delta) {
  if (state == nullptr) {
    return false;
  }

  const auto normalized = field.trimmed();
  if (normalized == QStringLiteral("exposure")) {
    state->exposure += delta;
  } else if (normalized == QStringLiteral("contrast")) {
    state->contrast += delta;
  } else if (normalized == QStringLiteral("highlights")) {
    state->highlights += delta;
  } else if (normalized == QStringLiteral("shadows")) {
    state->shadows += delta;
  } else if (normalized == QStringLiteral("whites")) {
    state->whites += delta;
  } else if (normalized == QStringLiteral("blacks")) {
    state->blacks += delta;
  } else if (normalized == QStringLiteral("temperature")) {
    state->temperature += delta;
  } else if (normalized == QStringLiteral("tint")) {
    state->tint += delta;
  } else if (normalized == QStringLiteral("texture")) {
    state->texture += delta;
  } else if (normalized == QStringLiteral("clarity")) {
    state->clarity += delta;
  } else if (normalized == QStringLiteral("dehaze")) {
    state->dehaze += delta;
  } else if (normalized == QStringLiteral("vibrance")) {
    state->vibrance += delta;
  } else if (normalized == QStringLiteral("saturation")) {
    state->saturation += delta;
  } else if (normalized == QStringLiteral("sharpening")) {
    state->sharpening += delta;
  } else if (normalized == QStringLiteral("luminanceNr")) {
    state->luminance_nr += delta;
  } else if (normalized == QStringLiteral("colorNr")) {
    state->color_nr += delta;
  } else {
    return false;
  }
  return true;
}

EditRecipeState adaptive_preset_state_for_context(
    const EditRecipeState& preset_state,
    const QJsonArray& rules,
    const QString& camera_label,
    const QString& lens_model,
    const QString& capture_date,
    int raw_highlight_clip_count,
    int raw_shadow_clip_count) {
  auto adapted_state = preset_state;
  for (const auto& rule_value : rules) {
    if (!rule_value.isObject()) {
      continue;
    }
    const auto rule = rule_value.toObject();
    if (!adaptive_rule_matches(
            rule,
            camera_label,
            lens_model,
            capture_date,
            raw_highlight_clip_count,
            raw_shadow_clip_count)) {
      continue;
    }
    apply_adaptive_delta(
        &adapted_state,
        rule.value(QStringLiteral("field")).toString(),
        rule.value(QStringLiteral("delta")).toDouble());
  }
  return adapted_state;
}

int preset_thumbnail_channel(double value) {
  return qBound(0, static_cast<int>(std::lround(clamp_unit(value) * 255.0)), 255);
}

QString preset_thumbnail_source_for_state(const EditRecipeState& state) {
  constexpr auto width = 96;
  constexpr auto height = 64;
  QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
  const auto exposure_gain = std::pow(2.0, state.exposure);
  const auto contrast_scale = std::max(0.2, 1.0 + state.contrast / 100.0);
  const auto saturation_scale = std::max(0.0, 1.0 + (state.vibrance + state.saturation) / 180.0);
  const auto temperature_bias = state.temperature / 100.0;
  const auto tint_bias = state.tint / 140.0;

  for (int y = 0; y < height; ++y) {
    auto* scanline = reinterpret_cast<QRgb*>(image.scanLine(y));
    const auto vertical = static_cast<double>(y) / static_cast<double>(height - 1);
    for (int x = 0; x < width; ++x) {
      const auto horizontal = static_cast<double>(x) / static_cast<double>(width - 1);
      auto red = 0.18 + horizontal * 0.62 + (1.0 - vertical) * 0.10;
      auto green = 0.22 + (1.0 - vertical) * 0.48 + horizontal * 0.08;
      auto blue = 0.24 + (1.0 - horizontal) * 0.36 + vertical * 0.16;

      red += temperature_bias * 0.28;
      blue -= temperature_bias * 0.22;
      green += tint_bias * 0.18;
      red *= exposure_gain;
      green *= exposure_gain;
      blue *= exposure_gain;

      red = ((red - 0.5) * contrast_scale) + 0.5;
      green = ((green - 0.5) * contrast_scale) + 0.5;
      blue = ((blue - 0.5) * contrast_scale) + 0.5;

      const auto luminance = red * 0.2126 + green * 0.7152 + blue * 0.0722;
      red = luminance + (red - luminance) * saturation_scale;
      green = luminance + (green - luminance) * saturation_scale;
      blue = luminance + (blue - luminance) * saturation_scale;

      const auto highlight_weight = clamp_unit((horizontal + (1.0 - vertical)) * 0.5);
      const auto shadow_weight = 1.0 - highlight_weight;
      if (state.grading_shadows_saturation > 1e-6) {
        const auto tint = QColor::fromHsvF(
            static_cast<float>(clamp_unit(state.grading_shadows_hue)),
            static_cast<float>(clamp_unit(state.grading_shadows_saturation / 100.0)),
            1.0);
        const auto amount = shadow_weight * clamp_unit(state.grading_shadows_saturation / 100.0) * 0.22;
        red = red * (1.0 - amount) + tint.redF() * amount;
        green = green * (1.0 - amount) + tint.greenF() * amount;
        blue = blue * (1.0 - amount) + tint.blueF() * amount;
      }
      if (state.grading_highlights_saturation > 1e-6) {
        const auto tint = QColor::fromHsvF(
            static_cast<float>(clamp_unit(state.grading_highlights_hue)),
            static_cast<float>(clamp_unit(state.grading_highlights_saturation / 100.0)),
            1.0);
        const auto amount = highlight_weight * clamp_unit(state.grading_highlights_saturation / 100.0) * 0.22;
        red = red * (1.0 - amount) + tint.redF() * amount;
        green = green * (1.0 - amount) + tint.greenF() * amount;
        blue = blue * (1.0 - amount) + tint.blueF() * amount;
      }

      if (state.black_and_white_enabled) {
        red = luminance;
        green = luminance;
        blue = luminance;
      }

      scanline[x] = qRgba(
          preset_thumbnail_channel(red),
          preset_thumbnail_channel(green),
          preset_thumbnail_channel(blue),
          255);
    }
  }

  return encode_preview_png_source(image);
}

double interpolate_preset_value(double base, double target, double intensity) {
  return base + (target - base) * qBound(0.0, intensity, 1.0);
}

QVariantList interpolate_curve_points(
    const QVariantList& base,
    const QVariantList& target,
    double intensity) {
  if (intensity <= 1e-6) {
    return base;
  }
  if (intensity >= 1.0 - 1e-6) {
    return target;
  }
  if (base.size() != target.size()) {
    return base;
  }

  QVariantList mixed_points;
  mixed_points.reserve(base.size());
  for (auto index = 0; index < base.size(); ++index) {
    const auto base_point = base.at(index).toMap();
    const auto target_point = target.at(index).toMap();
    if (!base_point.contains(QStringLiteral("x")) || !base_point.contains(QStringLiteral("y")) ||
        !target_point.contains(QStringLiteral("x")) || !target_point.contains(QStringLiteral("y"))) {
      return base;
    }

    QVariantMap mixed_point;
    mixed_point.insert(
        QStringLiteral("x"),
        interpolate_preset_value(
            base_point.value(QStringLiteral("x")).toDouble(),
            target_point.value(QStringLiteral("x")).toDouble(),
            intensity));
    mixed_point.insert(
        QStringLiteral("y"),
        interpolate_preset_value(
            base_point.value(QStringLiteral("y")).toDouble(),
            target_point.value(QStringLiteral("y")).toDouble(),
            intensity));
    mixed_points.push_back(mixed_point);
  }
  return mixed_points;
}

EditRecipeState interpolate_preset_state(
    const EditRecipeState& base,
    const EditRecipeState& target,
    double intensity) {
  const auto clamped_intensity = qBound(0.0, intensity, 1.0);
  if (clamped_intensity <= 1e-6) {
    return base;
  }
  if (clamped_intensity >= 1.0 - 1e-6) {
    return target;
  }

  auto mixed = base;
  auto mix = [clamped_intensity](double base_value, double target_value) {
    return interpolate_preset_value(base_value, target_value, clamped_intensity);
  };

  mixed.raw_exposure = mix(base.raw_exposure, target.raw_exposure);
  mixed.raw_boost = mix(base.raw_boost, target.raw_boost);
  mixed.raw_shadow_boost = mix(base.raw_shadow_boost, target.raw_shadow_boost);
  mixed.raw_detail = mix(base.raw_detail, target.raw_detail);
  mixed.raw_chroma_noise_reduction =
      mix(base.raw_chroma_noise_reduction, target.raw_chroma_noise_reduction);
  mixed.raw_moire_reduction = mix(base.raw_moire_reduction, target.raw_moire_reduction);
  mixed.raw_hot_dead_pixel_cleanup =
      mix(base.raw_hot_dead_pixel_cleanup, target.raw_hot_dead_pixel_cleanup);
  mixed.raw_false_color_suppression =
      mix(base.raw_false_color_suppression, target.raw_false_color_suppression);
  mixed.raw_white_balance_temperature =
      mix(base.raw_white_balance_temperature, target.raw_white_balance_temperature);
  mixed.raw_white_balance_tint = mix(base.raw_white_balance_tint, target.raw_white_balance_tint);
  mixed.raw_black_level = mix(base.raw_black_level, target.raw_black_level);
  mixed.raw_white_level = mix(base.raw_white_level, target.raw_white_level);
  mixed.raw_calibration_hue = mix(base.raw_calibration_hue, target.raw_calibration_hue);
  mixed.raw_calibration_saturation =
      mix(base.raw_calibration_saturation, target.raw_calibration_saturation);
  mixed.raw_red_primary_hue = mix(base.raw_red_primary_hue, target.raw_red_primary_hue);
  mixed.raw_red_primary_saturation =
      mix(base.raw_red_primary_saturation, target.raw_red_primary_saturation);
  mixed.raw_green_primary_hue = mix(base.raw_green_primary_hue, target.raw_green_primary_hue);
  mixed.raw_green_primary_saturation =
      mix(base.raw_green_primary_saturation, target.raw_green_primary_saturation);
  mixed.raw_blue_primary_hue = mix(base.raw_blue_primary_hue, target.raw_blue_primary_hue);
  mixed.raw_blue_primary_saturation =
      mix(base.raw_blue_primary_saturation, target.raw_blue_primary_saturation);

  mixed.exposure = mix(base.exposure, target.exposure);
  mixed.contrast = mix(base.contrast, target.contrast);
  mixed.highlights = mix(base.highlights, target.highlights);
  mixed.shadows = mix(base.shadows, target.shadows);
  mixed.whites = mix(base.whites, target.whites);
  mixed.blacks = mix(base.blacks, target.blacks);
  mixed.brightness = mix(base.brightness, target.brightness);
  mixed.hdr_local_tone_mapping = mix(base.hdr_local_tone_mapping, target.hdr_local_tone_mapping);
  mixed.filmic_shoulder = mix(base.filmic_shoulder, target.filmic_shoulder);
  mixed.filmic_toe = mix(base.filmic_toe, target.filmic_toe);
  mixed.adaptive_contrast = mix(base.adaptive_contrast, target.adaptive_contrast);
  mixed.levels_black_point = mix(base.levels_black_point, target.levels_black_point);
  mixed.levels_gamma = mix(base.levels_gamma, target.levels_gamma);
  mixed.levels_white_point = mix(base.levels_white_point, target.levels_white_point);
  mixed.zone_deep_shadows = mix(base.zone_deep_shadows, target.zone_deep_shadows);
  mixed.zone_shadows = mix(base.zone_shadows, target.zone_shadows);
  mixed.zone_midtones = mix(base.zone_midtones, target.zone_midtones);
  mixed.zone_lights = mix(base.zone_lights, target.zone_lights);
  mixed.zone_highlights = mix(base.zone_highlights, target.zone_highlights);
  mixed.temperature = mix(base.temperature, target.temperature);
  mixed.tint = mix(base.tint, target.tint);
  mixed.texture = mix(base.texture, target.texture);
  mixed.clarity = mix(base.clarity, target.clarity);
  mixed.dehaze = mix(base.dehaze, target.dehaze);
  mixed.sharpening = mix(base.sharpening, target.sharpening);
  mixed.sharpen_masking = mix(base.sharpen_masking, target.sharpen_masking);
  mixed.luminance_nr = mix(base.luminance_nr, target.luminance_nr);
  mixed.color_nr = mix(base.color_nr, target.color_nr);
  mixed.lens_distortion = mix(base.lens_distortion, target.lens_distortion);
  mixed.lens_vignette = mix(base.lens_vignette, target.lens_vignette);
  mixed.chromatic_aberration = mix(base.chromatic_aberration, target.chromatic_aberration);
  mixed.perspective_vertical = mix(base.perspective_vertical, target.perspective_vertical);
  mixed.perspective_horizontal = mix(base.perspective_horizontal, target.perspective_horizontal);
  mixed.transform_rotation = mix(base.transform_rotation, target.transform_rotation);
  mixed.transform_scale = mix(base.transform_scale, target.transform_scale);
  mixed.transform_skew_x = mix(base.transform_skew_x, target.transform_skew_x);
  mixed.transform_skew_y = mix(base.transform_skew_y, target.transform_skew_y);
  mixed.liquify_center_x = mix(base.liquify_center_x, target.liquify_center_x);
  mixed.liquify_center_y = mix(base.liquify_center_y, target.liquify_center_y);
  mixed.liquify_radius = mix(base.liquify_radius, target.liquify_radius);
  mixed.liquify_strength = mix(base.liquify_strength, target.liquify_strength);
  mixed.vibrance = mix(base.vibrance, target.vibrance);
  mixed.saturation = mix(base.saturation, target.saturation);
  mixed.curve_shadows = mix(base.curve_shadows, target.curve_shadows);
  mixed.curve_darks = mix(base.curve_darks, target.curve_darks);
  mixed.curve_lights = mix(base.curve_lights, target.curve_lights);
  mixed.curve_highlights = mix(base.curve_highlights, target.curve_highlights);
  mixed.master_curve_points =
      interpolate_curve_points(base.master_curve_points, target.master_curve_points, clamped_intensity);
  mixed.luma_curve_points =
      interpolate_curve_points(base.luma_curve_points, target.luma_curve_points, clamped_intensity);
  mixed.red_curve_points =
      interpolate_curve_points(base.red_curve_points, target.red_curve_points, clamped_intensity);
  mixed.green_curve_points =
      interpolate_curve_points(base.green_curve_points, target.green_curve_points, clamped_intensity);
  mixed.blue_curve_points =
      interpolate_curve_points(base.blue_curve_points, target.blue_curve_points, clamped_intensity);
  mixed.hue_adjustment = mix(base.hue_adjustment, target.hue_adjustment);
  mixed.saturation_adjustment = mix(base.saturation_adjustment, target.saturation_adjustment);
  mixed.luminance_adjustment = mix(base.luminance_adjustment, target.luminance_adjustment);

  for (std::size_t index = 0; index < mixed.mixer_channels.size(); ++index) {
    mixed.mixer_channels[index].hue =
        mix(base.mixer_channels[index].hue, target.mixer_channels[index].hue);
    mixed.mixer_channels[index].saturation =
        mix(base.mixer_channels[index].saturation, target.mixer_channels[index].saturation);
    mixed.mixer_channels[index].luminance =
        mix(base.mixer_channels[index].luminance, target.mixer_channels[index].luminance);
    mixed.mixer_channels[index].selective_cyan =
        mix(base.mixer_channels[index].selective_cyan, target.mixer_channels[index].selective_cyan);
    mixed.mixer_channels[index].selective_magenta =
        mix(base.mixer_channels[index].selective_magenta, target.mixer_channels[index].selective_magenta);
    mixed.mixer_channels[index].selective_yellow =
        mix(base.mixer_channels[index].selective_yellow, target.mixer_channels[index].selective_yellow);
    mixed.mixer_channels[index].selective_black =
        mix(base.mixer_channels[index].selective_black, target.mixer_channels[index].selective_black);
    mixed.black_and_white_mixer[index] =
        mix(base.black_and_white_mixer[index], target.black_and_white_mixer[index]);
  }

  mixed.rgb_channel_mixer.red_from_red =
      mix(base.rgb_channel_mixer.red_from_red, target.rgb_channel_mixer.red_from_red);
  mixed.rgb_channel_mixer.red_from_green =
      mix(base.rgb_channel_mixer.red_from_green, target.rgb_channel_mixer.red_from_green);
  mixed.rgb_channel_mixer.red_from_blue =
      mix(base.rgb_channel_mixer.red_from_blue, target.rgb_channel_mixer.red_from_blue);
  mixed.rgb_channel_mixer.green_from_red =
      mix(base.rgb_channel_mixer.green_from_red, target.rgb_channel_mixer.green_from_red);
  mixed.rgb_channel_mixer.green_from_green =
      mix(base.rgb_channel_mixer.green_from_green, target.rgb_channel_mixer.green_from_green);
  mixed.rgb_channel_mixer.green_from_blue =
      mix(base.rgb_channel_mixer.green_from_blue, target.rgb_channel_mixer.green_from_blue);
  mixed.rgb_channel_mixer.blue_from_red =
      mix(base.rgb_channel_mixer.blue_from_red, target.rgb_channel_mixer.blue_from_red);
  mixed.rgb_channel_mixer.blue_from_green =
      mix(base.rgb_channel_mixer.blue_from_green, target.rgb_channel_mixer.blue_from_green);
  mixed.rgb_channel_mixer.blue_from_blue =
      mix(base.rgb_channel_mixer.blue_from_blue, target.rgb_channel_mixer.blue_from_blue);
  mixed.color_replacement_source_hue =
      mix(base.color_replacement_source_hue, target.color_replacement_source_hue);
  mixed.color_replacement_target_hue =
      mix(base.color_replacement_target_hue, target.color_replacement_target_hue);
  mixed.color_replacement_range = mix(base.color_replacement_range, target.color_replacement_range);
  mixed.color_replacement_strength =
      mix(base.color_replacement_strength, target.color_replacement_strength);
  mixed.skin_tone_line_hue = mix(base.skin_tone_line_hue, target.skin_tone_line_hue);
  mixed.skin_tone_protection_range =
      mix(base.skin_tone_protection_range, target.skin_tone_protection_range);
  mixed.skin_tone_protection_strength =
      mix(base.skin_tone_protection_strength, target.skin_tone_protection_strength);
  mixed.color_harmony_base_hue = mix(base.color_harmony_base_hue, target.color_harmony_base_hue);
  mixed.color_harmony_strength = mix(base.color_harmony_strength, target.color_harmony_strength);
  mixed.grading_shadows_hue = mix(base.grading_shadows_hue, target.grading_shadows_hue);
  mixed.grading_shadows_saturation =
      mix(base.grading_shadows_saturation, target.grading_shadows_saturation);
  mixed.grading_shadows_luminance =
      mix(base.grading_shadows_luminance, target.grading_shadows_luminance);
  mixed.grading_midtones_hue = mix(base.grading_midtones_hue, target.grading_midtones_hue);
  mixed.grading_midtones_saturation =
      mix(base.grading_midtones_saturation, target.grading_midtones_saturation);
  mixed.grading_midtones_luminance =
      mix(base.grading_midtones_luminance, target.grading_midtones_luminance);
  mixed.grading_highlights_hue = mix(base.grading_highlights_hue, target.grading_highlights_hue);
  mixed.grading_highlights_saturation =
      mix(base.grading_highlights_saturation, target.grading_highlights_saturation);
  mixed.grading_highlights_luminance =
      mix(base.grading_highlights_luminance, target.grading_highlights_luminance);
  mixed.crop_rect_x = mix(base.crop_rect_x, target.crop_rect_x);
  mixed.crop_rect_y = mix(base.crop_rect_y, target.crop_rect_y);
  mixed.crop_rect_width = mix(base.crop_rect_width, target.crop_rect_width);
  mixed.crop_rect_height = mix(base.crop_rect_height, target.crop_rect_height);
  mixed.crop_zoom = mix(base.crop_zoom, target.crop_zoom);
  mixed.upright_vertical_guide_angle =
      mix(base.upright_vertical_guide_angle, target.upright_vertical_guide_angle);
  mixed.upright_horizontal_guide_angle =
      mix(base.upright_horizontal_guide_angle, target.upright_horizontal_guide_angle);
  mixed.output_headroom = mix(base.output_headroom, target.output_headroom);
  mixed.linear_mask_exposure = mix(base.linear_mask_exposure, target.linear_mask_exposure);
  mixed.linear_mask_temperature = mix(base.linear_mask_temperature, target.linear_mask_temperature);
  mixed.linear_mask_feather = mix(base.linear_mask_feather, target.linear_mask_feather);
  mixed.linear_mask_position = mix(base.linear_mask_position, target.linear_mask_position);
  mixed.linear_mask_rotation = mix(base.linear_mask_rotation, target.linear_mask_rotation);
  mixed.radial_mask_exposure = mix(base.radial_mask_exposure, target.radial_mask_exposure);
  mixed.radial_mask_temperature = mix(base.radial_mask_temperature, target.radial_mask_temperature);
  mixed.radial_mask_feather = mix(base.radial_mask_feather, target.radial_mask_feather);
  mixed.radial_mask_center_x = mix(base.radial_mask_center_x, target.radial_mask_center_x);
  mixed.radial_mask_center_y = mix(base.radial_mask_center_y, target.radial_mask_center_y);
  mixed.radial_mask_radius_x = mix(base.radial_mask_radius_x, target.radial_mask_radius_x);
  mixed.radial_mask_radius_y = mix(base.radial_mask_radius_y, target.radial_mask_radius_y);
  mixed.radial_mask_rotation = mix(base.radial_mask_rotation, target.radial_mask_rotation);
  return mixed;
}

QVariantList parametric_curve_points(double shadows, double darks, double lights, double highlights) {
  const auto point_output = [](double x, double delta) {
    return clamp_unit(x + (delta / 100.0) * 0.34);
  };

  return {
      curve_point(0.0, 0.0),
      curve_point(0.20, point_output(0.20, shadows)),
      curve_point(0.40, point_output(0.40, darks)),
      curve_point(0.65, point_output(0.65, lights)),
      curve_point(0.85, point_output(0.85, highlights)),
      curve_point(1.0, 1.0),
  };
}

bool is_identity_curve_points(const QVariantList& points) {
  if (points.size() != 2) {
    return false;
  }

  return std::abs(curve_point_coordinate(points[0], QStringLiteral("x"), 0.0) - 0.0) <= 1e-6 &&
         std::abs(curve_point_coordinate(points[0], QStringLiteral("y"), 0.0) - 0.0) <= 1e-6 &&
         std::abs(curve_point_coordinate(points[1], QStringLiteral("x"), 1.0) - 1.0) <= 1e-6 &&
         std::abs(curve_point_coordinate(points[1], QStringLiteral("y"), 1.0) - 1.0) <= 1e-6;
}

QVariantList normalize_curve_points(const QVariantList& points) {
  std::vector<QVariantMap> interior_points;
  interior_points.reserve(static_cast<std::size_t>(points.size()));
  for (const auto& value : points) {
    const auto point = value.toMap();
    if (point.isEmpty()) {
      continue;
    }
    interior_points.push_back(curve_point(
        point.value(QStringLiteral("x")).toDouble(),
        point.value(QStringLiteral("y")).toDouble()));
  }

  std::sort(interior_points.begin(), interior_points.end(), [](const QVariantMap& left, const QVariantMap& right) {
    return left.value(QStringLiteral("x")).toDouble() < right.value(QStringLiteral("x")).toDouble();
  });

  auto black_point_y = 0.0;
  auto white_point_y = 1.0;
  QVariantList normalized;
  auto previous_x = 0.0;
  for (const auto& point : interior_points) {
    auto x = clamp_unit(point.value(QStringLiteral("x")).toDouble());
    auto y = clamp_unit(point.value(QStringLiteral("y")).toDouble());
    if (x <= 0.0 + 1e-6 || x >= 1.0 - 1e-6) {
      if (x <= 0.0 + 1e-6) {
        black_point_y = y;
      } else {
        white_point_y = y;
      }
      continue;
    }

    x = std::max(previous_x + 0.001, x);
    x = std::min(0.999, x);
    if (x >= 0.999) {
      continue;
    }

    normalized.append(curve_point(x, y));
    previous_x = x;
  }
  normalized.prepend(curve_point(0.0, black_point_y));
  normalized.append(curve_point(1.0, white_point_y));

  if (is_identity_curve_points(normalized)) {
    return {};
  }
  return normalized;
}

std::vector<photo_editor::pipeline::CurvePoint> pipeline_curve_points(const QVariantList& points) {
  std::vector<photo_editor::pipeline::CurvePoint> curve_points;
  curve_points.reserve(static_cast<std::size_t>(points.size()));
  for (const auto& value : points) {
    curve_points.push_back(photo_editor::pipeline::CurvePoint{
        .x = curve_point_coordinate(value, QStringLiteral("x"), 0.0),
        .y = curve_point_coordinate(value, QStringLiteral("y"), 0.0),
    });
  }
  return curve_points;
}

void populate_inpaint_patch(
    photo_editor::pipeline::LocalMask* local_mask,
    const QVariantMap& mask) {
  if (local_mask == nullptr) {
    return;
  }

  const auto patch_data = mask.value(QStringLiteral("inpaintPatchData")).toByteArray();
  const auto patch_x = mask.value(QStringLiteral("inpaintPatchX")).toInt();
  const auto patch_y = mask.value(QStringLiteral("inpaintPatchY")).toInt();
  const auto patch_width = mask.value(QStringLiteral("inpaintPatchWidth")).toInt();
  const auto patch_height = mask.value(QStringLiteral("inpaintPatchHeight")).toInt();
  if (patch_data.isEmpty() || patch_width <= 0 || patch_height <= 0) {
    return;
  }

  auto patch_image = QImage::fromData(patch_data, "PNG");
  if (patch_image.isNull() || patch_image.width() != patch_width ||
      patch_image.height() != patch_height) {
    return;
  }
  if (!patch_image.colorSpace().isValid()) {
    patch_image.setColorSpace(preview_target_color_space());
  }

  const auto working_patch = working_image_from_qimage(patch_image);
  if (working_patch.isNull()) {
    return;
  }

  local_mask->inpaint_patch_pixels = working_patch.pixels;
  local_mask->inpaint_patch_x = patch_x;
  local_mask->inpaint_patch_y = patch_y;
  local_mask->inpaint_patch_width = working_patch.width;
  local_mask->inpaint_patch_height = working_patch.height;
  local_mask->inpaint_model_name =
      mask.value(QStringLiteral("inpaintModelName")).toString().toStdString();
  local_mask->inpaint_blend_amount =
      qBound(0.0, mask.value(QStringLiteral("inpaintBlendAmount"), 1.0).toDouble(), 1.0);
}

photo_editor::pipeline::LocalMask pipeline_local_mask(const QVariantMap& mask) {
  photo_editor::pipeline::LocalMask local_mask;
  local_mask.id = mask.value(QStringLiteral("id")).toString().toStdString();
  local_mask.name = mask.value(QStringLiteral("name")).toString().toStdString();
  local_mask.kind = normalized_mask_kind(mask.value(QStringLiteral("kind")).toString()).toStdString();
  local_mask.operation =
      normalized_mask_operation(mask.value(QStringLiteral("operation")).toString()).toStdString();
  local_mask.retouch_mode = mask.value(QStringLiteral("retouchMode")).toString().trimmed().toLower().toStdString();
  local_mask.enabled = map_bool_value(mask, QStringLiteral("enabled"), true);
  local_mask.exposure = map_double_value(mask, QStringLiteral("exposure"), 0.0);
  local_mask.contrast = map_double_value(mask, QStringLiteral("contrast"), 0.0);
  local_mask.highlights = map_double_value(mask, QStringLiteral("highlights"), 0.0);
  local_mask.shadows = map_double_value(mask, QStringLiteral("shadows"), 0.0);
  local_mask.whites = map_double_value(mask, QStringLiteral("whites"), 0.0);
  local_mask.blacks = map_double_value(mask, QStringLiteral("blacks"), 0.0);
  local_mask.temperature = map_double_value(mask, QStringLiteral("temperature"), 0.0);
  local_mask.tint = map_double_value(mask, QStringLiteral("tint"), 0.0);
  local_mask.vibrance = map_double_value(mask, QStringLiteral("vibrance"), 0.0);
  local_mask.saturation = map_double_value(mask, QStringLiteral("saturation"), 0.0);
  local_mask.hue_shift = map_double_value(mask, QStringLiteral("hueShift"), 0.0);
  local_mask.saturation_shift = map_double_value(mask, QStringLiteral("saturationShift"), 0.0);
  local_mask.colorize_hue = map_double_value(mask, QStringLiteral("colorizeHue"), 0.0);
  local_mask.colorize_strength = map_double_value(mask, QStringLiteral("colorizeStrength"), 0.0);
  local_mask.remove_strength = map_double_value(mask, QStringLiteral("removeStrength"), 0.0);
  local_mask.selection_invert = map_bool_value(mask, QStringLiteral("selectionInvert"), false);
  local_mask.selection_expand = map_double_value(mask, QStringLiteral("selectionExpand"), 0.0);
  local_mask.selection_smooth = map_double_value(mask, QStringLiteral("selectionSmooth"), 0.0);
  local_mask.selection_fine_edge = map_double_value(mask, QStringLiteral("selectionFineEdge"), 0.0);
  local_mask.selection_edge_contrast =
      map_double_value(mask, QStringLiteral("selectionEdgeContrast"), 0.0);
  local_mask.selection_decontaminate =
      map_double_value(mask, QStringLiteral("selectionDecontaminate"), 0.0);
  local_mask.refine_luminance_min = map_double_value(mask, QStringLiteral("refineLuminanceMin"), 0.0);
  local_mask.refine_luminance_max = map_double_value(mask, QStringLiteral("refineLuminanceMax"), 1.0);
  local_mask.refine_hue_center = map_double_value(mask, QStringLiteral("refineHueCenter"), 0.0);
  local_mask.refine_hue_range = map_double_value(mask, QStringLiteral("refineHueRange"), 0.5);
  local_mask.refine_saturation_min = map_double_value(mask, QStringLiteral("refineSaturationMin"), 0.0);
  local_mask.refine_saturation_max = map_double_value(mask, QStringLiteral("refineSaturationMax"), 1.0);
  local_mask.refine_depth_min = map_double_value(mask, QStringLiteral("refineDepthMin"), 0.0);
  local_mask.refine_depth_max = map_double_value(mask, QStringLiteral("refineDepthMax"), 1.0);
  local_mask.refine_edge_confidence_min =
      map_double_value(mask, QStringLiteral("refineEdgeConfidenceMin"), 0.0);
  local_mask.refine_range_feather = map_double_value(mask, QStringLiteral("refineRangeFeather"), 0.05);
  local_mask.feather = map_double_value(
      mask, QStringLiteral("feather"), local_mask.kind == "radial" ? 0.25 : 0.22);
  local_mask.rotation = map_double_value(mask, QStringLiteral("rotation"), 0.0);
  if (local_mask.kind == "radial") {
    local_mask.center_x = map_double_value(mask, QStringLiteral("centerX"), 0.5);
    local_mask.center_y = map_double_value(mask, QStringLiteral("centerY"), 0.5);
    local_mask.radius_x = map_double_value(mask, QStringLiteral("radiusX"), 0.25);
    local_mask.radius_y = map_double_value(mask, QStringLiteral("radiusY"), 0.25);
  } else if (local_mask.kind == "linear") {
    local_mask.position = map_double_value(mask, QStringLiteral("position"), 0.5);
  } else if (local_mask.kind == "brush") {
    local_mask.feather = map_double_value(mask, QStringLiteral("feather"), 0.75);
    local_mask.brush_size = map_double_value(mask, QStringLiteral("brushSize"), 0.08);
    local_mask.brush_flow = map_double_value(mask, QStringLiteral("brushFlow"), 1.0);
    local_mask.brush_hardness = map_double_value(mask, QStringLiteral("brushHardness"), 0.25);
    local_mask.brush_buildup = map_bool_value(mask, QStringLiteral("brushBuildup"), true);
    local_mask.clone_source_set = map_bool_value(mask, QStringLiteral("cloneSourceSet"), false);
    local_mask.clone_source_x = map_double_value(mask, QStringLiteral("cloneSourceX"), 0.5);
    local_mask.clone_source_y = map_double_value(mask, QStringLiteral("cloneSourceY"), 0.5);
    local_mask.clone_offset_x = map_double_value(mask, QStringLiteral("cloneOffsetX"), 0.0);
    local_mask.clone_offset_y = map_double_value(mask, QStringLiteral("cloneOffsetY"), 0.0);
    local_mask.clone_scale = map_double_value(mask, QStringLiteral("cloneScale"), 1.0);
    local_mask.clone_rotation = map_double_value(mask, QStringLiteral("cloneRotation"), 0.0);
    const auto points = mask.value(QStringLiteral("brushPoints")).toList();
    local_mask.brush_points.reserve(static_cast<std::size_t>(points.size()));
    for (const auto& point_value : points) {
      const auto point = point_value.toMap();
      local_mask.brush_points.push_back(photo_editor::pipeline::BrushPoint{
          .x = map_double_value(point, QStringLiteral("x"), 0.5),
          .y = map_double_value(point, QStringLiteral("y"), 0.5),
          .size = map_double_value(point, QStringLiteral("size"), local_mask.brush_size),
          .flow = map_double_value(point, QStringLiteral("flow"), local_mask.brush_flow),
          .pressure = map_double_value(point, QStringLiteral("pressure"), 1.0),
          .tilt_x = map_double_value(point, QStringLiteral("tiltX"), 0.0),
          .tilt_y = map_double_value(point, QStringLiteral("tiltY"), 0.0),
          .erase = map_bool_value(point, QStringLiteral("erase"), false),
      });
    }
    const auto seg_data = mask.value(QStringLiteral("segmentationData")).toByteArray();
    const auto seg_width = mask.value(QStringLiteral("segmentationWidth")).toInt();
    const auto seg_height = mask.value(QStringLiteral("segmentationHeight")).toInt();
    if (valid_segmentation_payload(seg_data, seg_width, seg_height)) {
      local_mask.segmentation_mask.resize(static_cast<std::size_t>(seg_data.size()));
      std::memcpy(local_mask.segmentation_mask.data(),
                  seg_data.constData(),
                  static_cast<std::size_t>(seg_data.size()));
      local_mask.segmentation_width = seg_width;
      local_mask.segmentation_height = seg_height;
    }
  } else if (local_mask.kind == "polygon" || local_mask.kind == "lasso") {
    local_mask.feather = map_double_value(mask, QStringLiteral("feather"), 0.06);
    const auto points = mask.value(QStringLiteral("polygonPoints")).toList();
    local_mask.polygon_points.reserve(static_cast<std::size_t>(points.size()));
    for (const auto& point_value : points) {
      const auto point = point_value.toMap();
      local_mask.polygon_points.push_back(photo_editor::pipeline::CurvePoint{
          .x = map_double_value(point, QStringLiteral("x"), 0.5),
          .y = map_double_value(point, QStringLiteral("y"), 0.5),
      });
    }
  } else if (local_mask.kind == "luminance") {
    local_mask.luminance_min = map_double_value(mask, QStringLiteral("luminanceMin"), 0.2);
    local_mask.luminance_max = map_double_value(mask, QStringLiteral("luminanceMax"), 0.8);
    local_mask.range_feather = map_double_value(mask, QStringLiteral("rangeFeather"), 0.1);
  } else if (local_mask.kind == "color") {
    local_mask.hue_center = map_double_value(mask, QStringLiteral("hueCenter"), 0.08);
    local_mask.hue_range = map_double_value(mask, QStringLiteral("hueRange"), 0.12);
    local_mask.saturation_min = map_double_value(mask, QStringLiteral("saturationMin"), 0.1);
    local_mask.saturation_max = map_double_value(mask, QStringLiteral("saturationMax"), 1.0);
    local_mask.range_feather = map_double_value(mask, QStringLiteral("rangeFeather"), 0.08);
  } else if (local_mask.kind == "depth") {
    local_mask.depth_min = map_double_value(mask, QStringLiteral("depthMin"), 0.2);
    local_mask.depth_max = map_double_value(mask, QStringLiteral("depthMax"), 0.8);
    local_mask.range_feather = map_double_value(mask, QStringLiteral("rangeFeather"), 0.1);
    const auto depth_data = mask.value(QStringLiteral("depthData")).toByteArray();
    const auto depth_width = mask.value(QStringLiteral("depthWidth")).toInt();
    const auto depth_height = mask.value(QStringLiteral("depthHeight")).toInt();
    if (valid_segmentation_payload(depth_data, depth_width, depth_height)) {
      local_mask.depth_map.resize(static_cast<std::size_t>(depth_data.size()));
      std::memcpy(local_mask.depth_map.data(),
                  depth_data.constData(),
                  static_cast<std::size_t>(depth_data.size()));
      local_mask.depth_width = depth_width;
      local_mask.depth_height = depth_height;
    }
  } else if (is_raster_mask_kind(QString::fromStdString(local_mask.kind))) {
    const auto seg_data = mask.value(QStringLiteral("segmentationData")).toByteArray();
    const auto seg_width = mask.value(QStringLiteral("segmentationWidth")).toInt();
    const auto seg_height = mask.value(QStringLiteral("segmentationHeight")).toInt();
    if (valid_segmentation_payload(seg_data, seg_width, seg_height)) {
      local_mask.segmentation_mask.resize(static_cast<std::size_t>(seg_data.size()));
      std::memcpy(local_mask.segmentation_mask.data(),
                  seg_data.constData(),
                  static_cast<std::size_t>(seg_data.size()));
      local_mask.segmentation_width = seg_width;
      local_mask.segmentation_height = seg_height;
    }
  }
  populate_inpaint_patch(&local_mask, mask);
  return local_mask;
}

double range_mask_weight_for_preview_pixel(
    double normalized_x,
    double normalized_y,
    const photo_editor::pipeline::ColorPixel& pixel,
    const photo_editor::pipeline::LocalMask& mask) {
  if (mask.kind == "luminance") {
    auto min_value = std::clamp(mask.luminance_min, 0.0, 1.0);
    auto max_value = std::clamp(mask.luminance_max, 0.0, 1.0);
    if (min_value > max_value) {
      std::swap(min_value, max_value);
    }
    const auto feather = std::clamp(mask.range_feather, 0.0, 0.5);
    const auto value = clamp_unit(preview_luminance_for_pixel(pixel));
    const auto lower_weight =
        smoothstep(std::max(0.0, min_value - feather), min_value, value);
    const auto upper_weight =
        1.0 - smoothstep(max_value, std::min(1.0, max_value + feather), value);
    return apply_full_refine_to_preview_weight(
        clamp_unit(lower_weight * upper_weight),
        normalized_x,
        normalized_y,
        pixel,
        mask);
  }

  if (mask.kind == "color") {
    double hue = 0.0;
    double saturation = 0.0;
    hue_saturation_value_for_preview_pixel(pixel, &hue, &saturation, nullptr);
    auto saturation_min = std::clamp(mask.saturation_min, 0.0, 1.0);
    auto saturation_max = std::clamp(mask.saturation_max, 0.0, 1.0);
    if (saturation_min > saturation_max) {
      std::swap(saturation_min, saturation_max);
    }
    const auto feather = std::clamp(mask.range_feather, 0.0, 0.5);
    const auto hue_weight = circular_weight(
        hue,
        wrap_unit(mask.hue_center),
        std::max(0.02, mask.hue_range));
    const auto sat_lower =
        smoothstep(std::max(0.0, saturation_min - feather), saturation_min, saturation);
    const auto sat_upper =
        1.0 - smoothstep(saturation_max, std::min(1.0, saturation_max + feather), saturation);
    return apply_full_refine_to_preview_weight(
        clamp_unit(hue_weight * sat_lower * sat_upper),
        normalized_x,
        normalized_y,
        pixel,
        mask);
  }

  if (mask.kind == "depth") {
    auto min_value = std::clamp(mask.depth_min, 0.0, 1.0);
    auto max_value = std::clamp(mask.depth_max, 0.0, 1.0);
    if (min_value > max_value) {
      std::swap(min_value, max_value);
    }
    const auto value = depth_refine_value_for_preview(normalized_x, normalized_y, pixel, mask);
    const auto feather = std::clamp(mask.range_feather, 0.0, 0.5);
    const auto lower_weight = min_value <= 1.0e-9
                                  ? 1.0
                                  : smoothstep(std::max(0.0, min_value - feather), min_value, value);
    const auto upper_weight = max_value >= 1.0 - 1.0e-9
                                  ? 1.0
                                  : 1.0 - smoothstep(
                                        max_value,
                                        std::min(1.0, max_value + feather),
                                        value);
    return apply_full_refine_to_preview_weight(
        clamp_unit(lower_weight * upper_weight),
        normalized_x,
        normalized_y,
        pixel,
        mask);
  }

  return 0.0;
}

QString range_mask_preview_source(
    const QImage& source_image,
    const photo_editor::pipeline::LocalMask& mask,
    const QVariantMap& mask_definition,
    const QString& preview_mode,
    double exposure_gain,
    const std::array<double, 3>& white_balance,
    double alpha_scale) {
  if (source_image.isNull() || source_image.width() <= 0 || source_image.height() <= 0 ||
      (mask.kind != "luminance" && mask.kind != "color" && mask.kind != "depth")) {
    return {};
  }

  const auto mode = mask_preview_mode_from_string(preview_mode);
  const auto width = source_image.width();
  const auto height = source_image.height();
  QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
  image.fill(preview_background_color_for_mode(mode));

  const auto color = preview_fill_color_for_mode(mask_definition, mode);
  for (int y = 0; y < height; ++y) {
    const auto* source_scanline =
        reinterpret_cast<const QRgb*>(source_image.constScanLine(y));
    auto* target_scanline = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = 0; x < width; ++x) {
      const auto pixel = QColor::fromRgb(source_scanline[x]);
      auto linear_pixel = photo_editor::pipeline::ColorPixel{
          .red = static_cast<float>(std::max(
              0.0,
              photo_editor::pipeline::srgb_to_linear(pixel.redF()) * exposure_gain *
                  white_balance[0])),
          .green = static_cast<float>(std::max(
              0.0,
              photo_editor::pipeline::srgb_to_linear(pixel.greenF()) * exposure_gain *
                  white_balance[1])),
          .blue = static_cast<float>(std::max(
              0.0,
              photo_editor::pipeline::srgb_to_linear(pixel.blueF()) * exposure_gain *
                  white_balance[2])),
          .alpha = pixel.alphaF(),
      };
      const auto weight = range_mask_weight_for_preview_pixel(
          static_cast<double>(x) / static_cast<double>(std::max(1, width - 1)),
          static_cast<double>(y) / static_cast<double>(std::max(1, height - 1)),
          linear_pixel,
          mask);
      const auto alpha = preview_fill_alpha_for_mode(weight, mode, alpha_scale, mask_definition);
      target_scanline[x] = qRgba(color.red(), color.green(), color.blue(), qBound(0, alpha, 255));
    }
  }

  return encode_preview_png_source(image);
}

double linear_mask_weight_for_preview(
    double normalized_x,
    double normalized_y,
    const photo_editor::pipeline::LocalMask& mask) {
  const auto radians = mask.rotation * M_PI / 180.0;
  const auto cosine = std::cos(radians);
  const auto sine = std::sin(radians);
  const auto axis_coordinate =
      ((normalized_x - 0.5) * cosine + (normalized_y - 0.5) * sine) + 0.5;
  return clamp_unit(((mask.position - axis_coordinate) / std::max(0.05, mask.feather)) + 0.5);
}

double radial_mask_weight_for_preview(
    double normalized_x,
    double normalized_y,
    const photo_editor::pipeline::LocalMask& mask) {
  const auto radius_x = std::max(0.05, mask.radius_x);
  const auto radius_y = std::max(0.05, mask.radius_y);
  const auto feather = std::clamp(mask.feather, 0.05, 1.0);
  const auto radians = mask.rotation * M_PI / 180.0;
  const auto cosine = std::cos(radians);
  const auto sine = std::sin(radians);
  const auto translated_x = normalized_x - mask.center_x;
  const auto translated_y = normalized_y - mask.center_y;
  const auto local_x = translated_x * cosine + translated_y * sine;
  const auto local_y = -translated_x * sine + translated_y * cosine;
  const auto distance = std::sqrt((local_x * local_x) / (radius_x * radius_x) +
                                  (local_y * local_y) / (radius_y * radius_y));
  const auto inner_radius = std::max(0.0, 1.0 - feather);
  if (distance <= inner_radius) {
    return 1.0;
  }
  if (distance >= 1.0) {
    return 0.0;
  }
  return 1.0 - smoothstep(inner_radius, 1.0, distance);
}

double bilinear_segmentation_weight_for_preview(
    double normalized_x,
    double normalized_y,
    const photo_editor::pipeline::LocalMask& mask) {
  const auto expected_mask_size =
      static_cast<std::size_t>(mask.segmentation_width) *
      static_cast<std::size_t>(mask.segmentation_height);
  if (mask.segmentation_mask.empty() || mask.segmentation_width <= 0 ||
      mask.segmentation_height <= 0 ||
      mask.segmentation_mask.size() != expected_mask_size) {
    return 0.0;
  }

  const auto max_x = std::max(mask.segmentation_width - 1, 0);
  const auto max_y = std::max(mask.segmentation_height - 1, 0);
  const auto fx = clamp_unit(normalized_x) * static_cast<double>(max_x);
  const auto fy = clamp_unit(normalized_y) * static_cast<double>(max_y);
  const auto ix0 = std::clamp(static_cast<int>(fx), 0, max_x);
  const auto iy0 = std::clamp(static_cast<int>(fy), 0, max_y);
  const auto ix1 = std::min(ix0 + 1, max_x);
  const auto iy1 = std::min(iy0 + 1, max_y);
  const auto dx = mask.segmentation_width > 1 ? fx - static_cast<double>(ix0) : 0.0;
  const auto dy = mask.segmentation_height > 1 ? fy - static_cast<double>(iy0) : 0.0;
  const auto index_for = [&](int x, int y) {
    return static_cast<std::size_t>(y * mask.segmentation_width + x);
  };
  const auto sample = [&](int x, int y) {
    return static_cast<double>(mask.segmentation_mask[index_for(x, y)]) / 255.0;
  };
  const auto v00 = sample(ix0, iy0);
  const auto v10 = sample(ix1, iy0);
  const auto v01 = sample(ix0, iy1);
  const auto v11 = sample(ix1, iy1);
  const auto top = v00 * (1.0 - dx) + v10 * dx;
  const auto bottom = v01 * (1.0 - dx) + v11 * dx;
  return clamp_unit(top * (1.0 - dy) + bottom * dy);
}

double brush_mask_weight_for_preview(
    double normalized_x,
    double normalized_y,
    const photo_editor::pipeline::LocalMask& mask) {
  if (!mask.segmentation_mask.empty() && mask.segmentation_width > 0 &&
      mask.segmentation_height > 0) {
    return bilinear_segmentation_weight_for_preview(normalized_x, normalized_y, mask);
  }

  auto weight = 0.0;
  const auto inner_scale = std::clamp(mask.brush_hardness, 0.0, 0.98);
  for (const auto& point : mask.brush_points) {
    const auto brush_size = std::max(0.01, point.size);
    const auto dx = normalized_x - point.x;
    const auto dy = normalized_y - point.y;
    const auto distance = std::sqrt(dx * dx + dy * dy) / brush_size;
    const auto point_weight =
        clamp_unit(point.flow) * (1.0 - smoothstep(inner_scale, 1.0, distance));
    weight = mask.brush_buildup ? 1.0 - (1.0 - weight) * (1.0 - point_weight)
                                : std::max(weight, point_weight);
  }
  return clamp_unit(weight);
}

double distance_to_segment_for_preview(
    double px,
    double py,
    double ax,
    double ay,
    double bx,
    double by) {
  const auto abx = bx - ax;
  const auto aby = by - ay;
  const auto ab_len_sq = abx * abx + aby * aby;
  if (ab_len_sq <= 1e-9) {
    return std::hypot(px - ax, py - ay);
  }
  const auto t = std::clamp(((px - ax) * abx + (py - ay) * aby) / ab_len_sq, 0.0, 1.0);
  const auto closest_x = ax + abx * t;
  const auto closest_y = ay + aby * t;
  return std::hypot(px - closest_x, py - closest_y);
}

bool polygon_contains_point_for_preview(
    double normalized_x,
    double normalized_y,
    const photo_editor::pipeline::LocalMask& mask) {
  const auto point_count = mask.polygon_points.size();
  if (point_count < 3) {
    return false;
  }

  auto inside = false;
  for (std::size_t i = 0, j = point_count - 1; i < point_count; j = i++) {
    const auto& pi = mask.polygon_points[i];
    const auto& pj = mask.polygon_points[j];
    const auto intersects =
        ((pi.y > normalized_y) != (pj.y > normalized_y)) &&
        (normalized_x <
         ((pj.x - pi.x) * (normalized_y - pi.y) / std::max(1e-9, pj.y - pi.y)) + pi.x);
    if (intersects) {
      inside = !inside;
    }
  }
  return inside;
}

double polygon_mask_weight_for_preview(
    double normalized_x,
    double normalized_y,
    const photo_editor::pipeline::LocalMask& mask) {
  const auto point_count = mask.polygon_points.size();
  if (point_count < 3) {
    return 0.0;
  }

  auto min_distance = std::numeric_limits<double>::max();
  for (std::size_t i = 0; i < point_count; ++i) {
    const auto& start = mask.polygon_points[i];
    const auto& end = mask.polygon_points[(i + 1) % point_count];
    min_distance = std::min(
        min_distance,
        distance_to_segment_for_preview(normalized_x, normalized_y, start.x, start.y, end.x, end.y));
  }

  const auto feather = std::clamp(mask.feather, 0.0, 0.25);
  if (polygon_contains_point_for_preview(normalized_x, normalized_y, mask)) {
    if (feather <= 1e-6) {
      return 1.0;
    }
    return 1.0 - 0.35 * smoothstep(0.0, feather, min_distance);
  }
  if (feather <= 1e-6) {
    return 0.0;
  }
  return 1.0 - smoothstep(0.0, feather, min_distance);
}

double subject_object_mask_weight_for_preview(
    double normalized_x,
    double normalized_y,
    const photo_editor::pipeline::LocalMask& mask) {
  const auto value = bilinear_segmentation_weight_for_preview(normalized_x, normalized_y, mask);
  const auto feather = std::max(0.01, mask.feather);
  const auto low = std::max(0.0, 0.5 - feather * 0.5);
  const auto high = std::min(1.0, 0.5 + feather * 0.5);
  return smoothstep(low, high, value);
}

double local_mask_weight_for_preview(
    double normalized_x,
    double normalized_y,
    const photo_editor::pipeline::ColorPixel& pixel,
    const photo_editor::pipeline::LocalMask& mask) {
  if (!mask.enabled) {
    return 0.0;
  }

  if (is_raster_mask_kind(QString::fromStdString(mask.kind))) {
    return apply_full_refine_to_preview_weight(
        subject_object_mask_weight_for_preview(normalized_x, normalized_y, mask),
        normalized_x,
        normalized_y,
        pixel,
        mask);
  }
  if (mask.kind == "radial") {
    return apply_full_refine_to_preview_weight(
        radial_mask_weight_for_preview(normalized_x, normalized_y, mask),
        normalized_x,
        normalized_y,
        pixel,
        mask);
  }
  if (mask.kind == "brush") {
    return apply_full_refine_to_preview_weight(
        brush_mask_weight_for_preview(normalized_x, normalized_y, mask),
        normalized_x,
        normalized_y,
        pixel,
        mask);
  }
  if (mask.kind == "polygon" || mask.kind == "lasso") {
    return apply_full_refine_to_preview_weight(
        polygon_mask_weight_for_preview(normalized_x, normalized_y, mask),
        normalized_x,
        normalized_y,
        pixel,
        mask);
  }
  if (mask.kind == "luminance" || mask.kind == "color" || mask.kind == "depth") {
    return range_mask_weight_for_preview_pixel(normalized_x, normalized_y, pixel, mask);
  }
  return apply_full_refine_to_preview_weight(
      linear_mask_weight_for_preview(normalized_x, normalized_y, mask),
      normalized_x,
      normalized_y,
      pixel,
      mask);
}

void accumulate_composite_weight_for_preview(
    double raw_weight,
    const QString& operation,
    double* composite_weight,
    bool* initialized) {
  if (composite_weight == nullptr || initialized == nullptr) {
    return;
  }

  if (operation == QStringLiteral("subtract")) {
    if (!*initialized) {
      return;
    }
    *composite_weight = std::max(0.0, *composite_weight - raw_weight);
    return;
  }

  if (operation == QStringLiteral("intersect")) {
    if (!*initialized) {
      return;
    }
    *composite_weight *= raw_weight;
    return;
  }

  if (!*initialized) {
    *composite_weight = raw_weight;
    *initialized = true;
    return;
  }

  *composite_weight = 1.0 - (1.0 - *composite_weight) * (1.0 - raw_weight);
}

QString composite_mask_preview_source(
    const QImage& source_image,
    const std::vector<photo_editor::pipeline::LocalMask>& masks,
    const QVariantMap& color_reference_mask,
    const QString& preview_mode,
    double exposure_gain,
    const std::array<double, 3>& white_balance,
    double alpha_scale) {
  if (source_image.isNull() || source_image.width() <= 0 || source_image.height() <= 0 ||
      masks.empty()) {
    return {};
  }

  const auto mode = mask_preview_mode_from_string(preview_mode);
  const auto width = source_image.width();
  const auto height = source_image.height();
  QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
  image.fill(preview_background_color_for_mode(mode));

  const auto color = preview_fill_color_for_mode(color_reference_mask, mode);
  for (int y = 0; y < height; ++y) {
    const auto* source_scanline =
        reinterpret_cast<const QRgb*>(source_image.constScanLine(y));
    auto* target_scanline = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = 0; x < width; ++x) {
      const auto rgb = QColor::fromRgb(source_scanline[x]);
      const auto pixel = photo_editor::pipeline::ColorPixel{
          .red = static_cast<float>(std::max(
              0.0,
              photo_editor::pipeline::srgb_to_linear(rgb.redF()) * exposure_gain *
                  white_balance[0])),
          .green = static_cast<float>(std::max(
              0.0,
              photo_editor::pipeline::srgb_to_linear(rgb.greenF()) * exposure_gain *
                  white_balance[1])),
          .blue = static_cast<float>(std::max(
              0.0,
              photo_editor::pipeline::srgb_to_linear(rgb.blueF()) * exposure_gain *
                  white_balance[2])),
          .alpha = rgb.alphaF(),
      };

      const auto normalized_x =
          width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
      const auto normalized_y =
          height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
      auto composite_weight = 0.0;
      auto initialized = false;
      for (const auto& mask : masks) {
        const auto raw_weight =
            local_mask_weight_for_preview(normalized_x, normalized_y, pixel, mask);
        accumulate_composite_weight_for_preview(
            raw_weight,
            QString::fromStdString(mask.operation),
            &composite_weight,
            &initialized);
      }

      const auto alpha = initialized
                             ? preview_fill_alpha_for_mode(
                                   composite_weight,
                                   mode,
                                   alpha_scale,
                                   color_reference_mask)
                             : 0;
      target_scanline[x] = qRgba(color.red(), color.green(), color.blue(), qBound(0, alpha, 255));
    }
  }

  return encode_preview_png_source(image);
}

QVariantList composite_mask_outline_paths(
    const QImage& source_image,
    const std::vector<photo_editor::pipeline::LocalMask>& masks,
    double exposure_gain,
    const std::array<double, 3>& white_balance) {
  if (source_image.isNull() || source_image.width() <= 0 || source_image.height() <= 0 ||
      masks.empty()) {
    return {};
  }

  const auto width = source_image.width();
  const auto height = source_image.height();
  QByteArray alpha_mask(width * height, '\0');
  for (int y = 0; y < height; ++y) {
    const auto* source_scanline =
        reinterpret_cast<const QRgb*>(source_image.constScanLine(y));
    for (int x = 0; x < width; ++x) {
      const auto rgb = QColor::fromRgb(source_scanline[x]);
      const auto pixel = photo_editor::pipeline::ColorPixel{
          .red = static_cast<float>(std::max(
              0.0,
              photo_editor::pipeline::srgb_to_linear(rgb.redF()) * exposure_gain *
                  white_balance[0])),
          .green = static_cast<float>(std::max(
              0.0,
              photo_editor::pipeline::srgb_to_linear(rgb.greenF()) * exposure_gain *
                  white_balance[1])),
          .blue = static_cast<float>(std::max(
              0.0,
              photo_editor::pipeline::srgb_to_linear(rgb.blueF()) * exposure_gain *
                  white_balance[2])),
          .alpha = rgb.alphaF(),
      };

      const auto normalized_x =
          width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
      const auto normalized_y =
          height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
      auto composite_weight = 0.0;
      auto initialized = false;
      for (const auto& mask : masks) {
        const auto raw_weight =
            local_mask_weight_for_preview(normalized_x, normalized_y, pixel, mask);
        accumulate_composite_weight_for_preview(
            raw_weight,
            QString::fromStdString(mask.operation),
            &composite_weight,
            &initialized);
      }

      alpha_mask[y * width + x] =
          static_cast<char>(initialized
                                ? static_cast<int>(std::lround(clamp_unit(composite_weight) * 255.0))
                                : 0);
    }
  }

  return traced_outline_paths_for_alpha_mask(alpha_mask, width, height);
}

std::vector<photo_editor::pipeline::LocalMaskSet> pipeline_local_mask_sets(const QVariantList& masks) {
  std::vector<photo_editor::pipeline::LocalMaskSet> mask_sets;
  QHash<QString, int> indices_by_id;
  auto selection_solo_active = false;
  auto solo_ordinal = 1;
  for (const auto& value : masks) {
    const auto mask = normalized_mask_definition(value.toMap(), solo_ordinal);
    ++solo_ordinal;
    if (mask_definition_affects_render(mask, solo_ordinal - 1) &&
        pipeline_mask_tool_role(mask) == QStringLiteral("selection") &&
        mask.value(QStringLiteral("solo")).toBool()) {
      selection_solo_active = true;
      break;
    }
  }
  auto ordinal = 1;
  for (const auto& value : masks) {
    const auto mask = normalized_mask_definition(value.toMap(), ordinal);
    ++ordinal;
    if (!mask_definition_affects_render(mask, ordinal - 1) ||
        pipeline_mask_tool_role(mask) != QStringLiteral("selection") ||
        (selection_solo_active && !mask.value(QStringLiteral("solo")).toBool())) {
      continue;
    }

    auto source = pipeline_local_mask(mask);
    source.exposure = 0.0;
    source.contrast = 0.0;
    source.highlights = 0.0;
    source.shadows = 0.0;
    source.whites = 0.0;
    source.blacks = 0.0;
    source.temperature = 0.0;
    source.tint = 0.0;
    source.vibrance = 0.0;
    source.saturation = 0.0;
    source.hue_shift = 0.0;
    source.saturation_shift = 0.0;
    source.colorize_hue = 0.0;
    source.colorize_strength = 0.0;
    source.remove_strength = 0.0;
    source.retouch_mode.clear();

    const auto set_id = mask.value(QStringLiteral("maskSetId")).toString();
    auto set_index = indices_by_id.value(set_id, -1);
    if (set_index < 0) {
      photo_editor::pipeline::LocalMaskSet set;
      set.id = set_id.toStdString();
      set.name = mask.value(QStringLiteral("maskSetName")).toString().toStdString();
      set.enabled = map_bool_value(mask, QStringLiteral("enabled"), true);
      set.exposure = map_double_value(mask, QStringLiteral("exposure"), 0.0);
      set.contrast = map_double_value(mask, QStringLiteral("contrast"), 0.0);
      set.highlights = map_double_value(mask, QStringLiteral("highlights"), 0.0);
      set.shadows = map_double_value(mask, QStringLiteral("shadows"), 0.0);
      set.whites = map_double_value(mask, QStringLiteral("whites"), 0.0);
      set.blacks = map_double_value(mask, QStringLiteral("blacks"), 0.0);
      set.temperature = map_double_value(mask, QStringLiteral("temperature"), 0.0);
      set.tint = map_double_value(mask, QStringLiteral("tint"), 0.0);
      set.vibrance = map_double_value(mask, QStringLiteral("vibrance"), 0.0);
      set.saturation = map_double_value(mask, QStringLiteral("saturation"), 0.0);
      set.hue_shift = map_double_value(mask, QStringLiteral("hueShift"), 0.0);
      set.saturation_shift = map_double_value(mask, QStringLiteral("saturationShift"), 0.0);
      set.colorize_hue = map_double_value(mask, QStringLiteral("colorizeHue"), 0.0);
      set.colorize_strength = map_double_value(mask, QStringLiteral("colorizeStrength"), 0.0);
      mask_sets.push_back(std::move(set));
      set_index = static_cast<int>(mask_sets.size()) - 1;
      indices_by_id.insert(set_id, set_index);
    }

    mask_sets[static_cast<std::size_t>(set_index)].sources.push_back(std::move(source));
  }
  return mask_sets;
}

std::vector<photo_editor::pipeline::LocalMask> pipeline_retouch_masks(const QVariantList& masks) {
  std::vector<photo_editor::pipeline::LocalMask> retouch_masks;
  retouch_masks.reserve(static_cast<std::size_t>(masks.size()));
  auto retouch_solo_active = false;
  auto solo_ordinal = 1;
  for (const auto& value : masks) {
    const auto mask = normalized_mask_definition(value.toMap(), solo_ordinal);
    ++solo_ordinal;
    if (mask_definition_affects_render(mask, solo_ordinal - 1) &&
        pipeline_mask_tool_role(mask) == QStringLiteral("retouch") &&
        mask.value(QStringLiteral("solo")).toBool()) {
      retouch_solo_active = true;
      break;
    }
  }
  auto ordinal = 1;
  for (const auto& value : masks) {
    const auto mask = normalized_mask_definition(value.toMap(), ordinal);
    ++ordinal;
    if (!mask_definition_affects_render(mask, ordinal - 1) ||
        pipeline_mask_tool_role(mask) != QStringLiteral("retouch") ||
        (retouch_solo_active && !mask.value(QStringLiteral("solo")).toBool())) {
      continue;
    }
    retouch_masks.push_back(pipeline_local_mask(mask));
  }
  return retouch_masks;
}

double layer_adjustment_value(const QVariantMap& layer, const QString& key) {
  const auto adjustments = layer.value(QStringLiteral("adjustments")).toMap();
  if (adjustments.contains(key)) {
    return adjustments.value(key).toDouble();
  }
  return layer.value(key).toDouble();
}

photo_editor::pipeline::ColorPixel layer_fill_color(const QVariantMap& layer) {
  auto color = QColor(layer.value(QStringLiteral("fillColor"), QStringLiteral("#808080")).toString());
  if (!color.isValid()) {
    color = QColor(QStringLiteral("#808080"));
  }
  return photo_editor::pipeline::ColorPixel{
      .red = static_cast<float>(color.redF()),
      .green = static_cast<float>(color.greenF()),
      .blue = static_cast<float>(color.blueF()),
      .alpha = static_cast<float>(qBound(0.0, layer.value(QStringLiteral("fillAlpha"), 1.0).toDouble(), 1.0)),
  };
}

QImage rasterized_text_layer_image(const QVariantMap& layer) {
  const auto text = layer.value(QStringLiteral("text"), QStringLiteral("Text")).toString();
  const auto width = qBound(16, layer.value(QStringLiteral("pixelWidth"), 512).toInt(), 8192);
  const auto height = qBound(16, layer.value(QStringLiteral("pixelHeight"), 128).toInt(), 8192);
  auto color = QColor(layer.value(QStringLiteral("textColor"), QStringLiteral("#ffffff")).toString());
  if (!color.isValid()) {
    color = QColor(QStringLiteral("#ffffff"));
  }

  QImage image(width, height, QImage::Format_RGBA8888);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  QFont font(layer.value(QStringLiteral("fontFamily"), QStringLiteral("Helvetica")).toString());
  font.setPixelSize(qBound(8, layer.value(QStringLiteral("fontSize"), 48).toInt(), 512));
  font.setBold(layer.value(QStringLiteral("fontWeight")).toString().trimmed().toLower() ==
               QStringLiteral("bold"));
  painter.setFont(font);
  painter.setPen(color);
  painter.drawText(image.rect().adjusted(8, 4, -8, -4), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, text);
  return image;
}

void populate_photo_layer_mask(photo_editor::pipeline::PhotoLayer* target, const QVariantMap& layer) {
  if (target == nullptr) {
    return;
  }
  const auto mask_data = layer.value(QStringLiteral("maskData")).toByteArray();
  const auto mask_width = layer.value(QStringLiteral("maskWidth")).toInt();
  const auto mask_height = layer.value(QStringLiteral("maskHeight")).toInt();
  const auto expected_size = mask_width * mask_height;
  if (mask_data.isEmpty() || mask_width <= 0 || mask_height <= 0 ||
      mask_data.size() != expected_size) {
    return;
  }
  target->mask_data.assign(mask_data.cbegin(), mask_data.cend());
  target->mask_width = mask_width;
  target->mask_height = mask_height;
  target->mask_inverted = layer.value(QStringLiteral("maskInverted")).toBool();
}

void populate_photo_layer_pixels(photo_editor::pipeline::PhotoLayer* target, const QVariantMap& layer) {
  if (target == nullptr) {
    return;
  }
  const auto kind = layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
  const auto pixel_data = layer.value(QStringLiteral("pixelData")).toByteArray();
  const auto pixel_width = layer.value(QStringLiteral("pixelWidth")).toInt();
  const auto pixel_height = layer.value(QStringLiteral("pixelHeight")).toInt();
  QImage image;
  if (!pixel_data.isEmpty() && pixel_width > 0 && pixel_height > 0) {
    image = QImage::fromData(pixel_data, "PNG");
    if (image.isNull() || image.width() != pixel_width || image.height() != pixel_height) {
      return;
    }
  } else if (kind == QStringLiteral("text")) {
    image = rasterized_text_layer_image(layer);
  } else if (kind == QStringLiteral("source")) {
    const auto source_path = layer.value(QStringLiteral("sourcePath")).toString();
    if (source_path.isEmpty()) {
      return;
    }
    image = QImage(source_path);
    if (image.isNull()) {
      return;
    }
    const auto target_width = layer.value(QStringLiteral("pixelWidth")).toInt();
    const auto target_height = layer.value(QStringLiteral("pixelHeight")).toInt();
    if (target_width > 0 && target_height > 0 &&
        (image.width() != target_width || image.height() != target_height)) {
      image = image.scaled(target_width, target_height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
  } else {
    return;
  }
  if (!image.colorSpace().isValid()) {
    image.setColorSpace(preview_target_color_space());
  }
  const auto working = working_image_from_qimage(image);
  if (working.isNull()) {
    return;
  }
  target->pixels = working.pixels;
  target->pixel_width = working.width;
  target->pixel_height = working.height;
  target->x = layer.value(QStringLiteral("x")).toInt();
  target->y = layer.value(QStringLiteral("y")).toInt();
  target->transform_scale = qBound(0.05, layer.value(QStringLiteral("scale"), 1.0).toDouble(), 32.0);
  target->transform_rotation = qBound(-3600.0, layer.value(QStringLiteral("rotation")).toDouble(), 3600.0);
}

std::vector<photo_editor::pipeline::PhotoLayer> pipeline_photo_layers(const QVariantList& layers) {
  std::vector<photo_editor::pipeline::PhotoLayer> photo_layers;
  photo_layers.reserve(static_cast<std::size_t>(layers.size()));
  auto solo_active = false;
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    const auto kind = layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
    if (kind != QStringLiteral("group") && layer.value(QStringLiteral("solo")).toBool()) {
      solo_active = true;
      break;
    }
  }
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    const auto kind = layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
    if (solo_active && kind != QStringLiteral("group") && !layer.value(QStringLiteral("solo")).toBool()) {
      continue;
    }
    auto photo_layer = photo_editor::pipeline::PhotoLayer{
        .id = layer.value(QStringLiteral("id")).toString().toStdString(),
        .name = layer.value(QStringLiteral("name")).toString().toStdString(),
        .kind = (kind.isEmpty() ? QStringLiteral("adjustment") : kind).toStdString(),
        .blend_mode =
            normalized_mask_blend_mode(layer.value(QStringLiteral("blendMode")).toString()).toStdString(),
        .enabled = map_bool_value(layer, QStringLiteral("enabled"), true),
        .opacity = qBound(0.0, layer.value(QStringLiteral("opacity"), 1.0).toDouble(), 1.0),
        .clipping_mask = layer.value(QStringLiteral("clippingMask")).toBool(),
        .exposure = layer_adjustment_value(layer, QStringLiteral("exposure")),
        .contrast = layer_adjustment_value(layer, QStringLiteral("contrast")),
        .highlights = layer_adjustment_value(layer, QStringLiteral("highlights")),
        .shadows = layer_adjustment_value(layer, QStringLiteral("shadows")),
        .whites = layer_adjustment_value(layer, QStringLiteral("whites")),
        .blacks = layer_adjustment_value(layer, QStringLiteral("blacks")),
        .temperature = layer_adjustment_value(layer, QStringLiteral("temperature")),
        .tint = layer_adjustment_value(layer, QStringLiteral("tint")),
        .vibrance = layer_adjustment_value(layer, QStringLiteral("vibrance")),
        .saturation = layer_adjustment_value(layer, QStringLiteral("saturation")),
        .hue_shift = layer_adjustment_value(layer, QStringLiteral("hueShift")),
        .saturation_shift = layer_adjustment_value(layer, QStringLiteral("saturationShift")),
        .colorize_hue = layer_adjustment_value(layer, QStringLiteral("colorizeHue")),
        .colorize_strength = layer_adjustment_value(layer, QStringLiteral("colorizeStrength")),
        .fill_color = layer_fill_color(layer),
    };
    populate_photo_layer_mask(&photo_layer, layer);
    populate_photo_layer_pixels(&photo_layer, layer);
    photo_layers.push_back(std::move(photo_layer));
  }
  return photo_layers;
}

photo_editor::pipeline::ColorAdjustmentState to_pipeline_state(
    const EditState& state,
    const photo_editor::pipeline::DecodedImageMetadata* metadata = nullptr) {
  return photo_editor::pipeline::ColorAdjustmentState{
      .input_transform =
          photo_editor::pipeline::InputTransformSettings{
              .input_profile_name = normalized_input_color_profile_name(state.input_color_profile).toStdString(),
              .exposure = state.exposure,
              .temperature = state.temperature,
              .tint = state.tint,
              .raw_input_transform_enabled = state.raw_input_transform_enabled,
              .highlight_recovery_enabled = state.raw_highlight_recovery,
              .highlight_reconstruction_mode =
                  normalized_highlight_reconstruction_mode_name(state.highlight_reconstruction_mode).toStdString(),
              .chromatic_adaptation_method =
                  normalized_chromatic_adaptation_method_name(state.chromatic_adaptation_method).toStdString(),
              .gamut_mapping_enabled = state.raw_gamut_mapping,
              .lens_correction_enabled = state.raw_lens_correction,
              .raw_white_balance_override_enabled = state.raw_white_balance_override,
              .camera_matching_look =
                  normalized_camera_matching_look_name(state.raw_camera_matching_look).toStdString(),
              .raw_white_balance_temperature = state.raw_white_balance_temperature,
              .raw_white_balance_tint = state.raw_white_balance_tint,
              .raw_black_level = state.raw_black_level,
              .raw_white_level = state.raw_white_level,
              .calibration_hue = state.raw_calibration_hue,
              .calibration_saturation = state.raw_calibration_saturation,
              .red_primary_hue = state.raw_red_primary_hue,
              .red_primary_saturation = state.raw_red_primary_saturation,
              .green_primary_hue = state.raw_green_primary_hue,
              .green_primary_saturation = state.raw_green_primary_saturation,
              .blue_primary_hue = state.raw_blue_primary_hue,
              .blue_primary_saturation = state.raw_blue_primary_saturation,
          },
      .color_pipeline = color_pipeline_config_for_state(state, metadata),
      .exposure = state.exposure,
      .contrast = state.contrast,
      .highlights = state.highlights,
      .shadows = state.shadows,
      .whites = state.whites,
      .blacks = state.blacks,
      .brightness = state.brightness,
      .hdr_local_tone_mapping = state.hdr_local_tone_mapping,
      .filmic_shoulder = state.filmic_shoulder,
      .filmic_toe = state.filmic_toe,
      .adaptive_contrast = state.adaptive_contrast,
      .levels_black_point = state.levels_black_point,
      .levels_gamma = state.levels_gamma,
      .levels_white_point = state.levels_white_point,
      .zone_deep_shadows = state.zone_deep_shadows,
      .zone_shadows = state.zone_shadows,
      .zone_midtones = state.zone_midtones,
      .zone_lights = state.zone_lights,
      .zone_highlights = state.zone_highlights,
      .temperature = state.temperature,
      .tint = state.tint,
      .texture = state.texture,
      .clarity = state.clarity,
      .dehaze = state.dehaze,
      .sharpening = state.sharpening,
      .sharpen_masking = state.sharpen_masking,
      .luminance_nr = state.luminance_nr,
      .color_nr = state.color_nr,
      .lens_distortion = state.lens_distortion,
      .lens_vignette = state.lens_vignette,
      .chromatic_aberration = state.chromatic_aberration,
      .perspective_vertical = state.perspective_vertical,
      .perspective_horizontal = state.perspective_horizontal,
      .transform_rotation = state.transform_rotation,
      .transform_scale = state.transform_scale,
      .transform_skew_x = state.transform_skew_x,
      .transform_skew_y = state.transform_skew_y,
      .liquify_center_x = state.liquify_center_x,
      .liquify_center_y = state.liquify_center_y,
      .liquify_radius = state.liquify_radius,
      .liquify_strength = state.liquify_strength,
      .content_aware_crop_fill = state.content_aware_crop_fill,
      .vibrance = state.vibrance,
      .saturation = state.saturation,
      .curve_shadows = state.curve_shadows,
      .curve_darks = state.curve_darks,
      .curve_lights = state.curve_lights,
      .curve_highlights = state.curve_highlights,
      .master_curve_points = pipeline_curve_points(state.master_curve_points),
      .luma_curve_points = pipeline_curve_points(state.luma_curve_points),
      .red_curve_points = pipeline_curve_points(state.red_curve_points),
      .green_curve_points = pipeline_curve_points(state.green_curve_points),
      .blue_curve_points = pipeline_curve_points(state.blue_curve_points),
      .color_mixer_model = state.color_mixer_model.toStdString(),
      .color_mixer_channel = state.hsl_channel.toStdString(),
      .hue_adjustment = state.hue_adjustment,
      .saturation_adjustment = state.saturation_adjustment,
      .luminance_adjustment = state.luminance_adjustment,
      .color_mixer_channels = [&]() {
        std::array<photo_editor::pipeline::ColorMixerChannelAdjustment,
                   photo_editor::pipeline::kColorMixerChannelCount>
            channels{};
        for (std::size_t i = 0; i < channels.size(); ++i) {
          channels[i].hue = state.mixer_channels[i].hue;
          channels[i].saturation = state.mixer_channels[i].saturation;
          channels[i].luminance = state.mixer_channels[i].luminance;
          channels[i].selective_cyan = state.mixer_channels[i].selective_cyan;
          channels[i].selective_magenta = state.mixer_channels[i].selective_magenta;
          channels[i].selective_yellow = state.mixer_channels[i].selective_yellow;
          channels[i].selective_black = state.mixer_channels[i].selective_black;
        }
        return channels;
      }(),
      .rgb_channel_mixer = {
          .red_from_red = state.rgb_channel_mixer.red_from_red,
          .red_from_green = state.rgb_channel_mixer.red_from_green,
          .red_from_blue = state.rgb_channel_mixer.red_from_blue,
          .green_from_red = state.rgb_channel_mixer.green_from_red,
          .green_from_green = state.rgb_channel_mixer.green_from_green,
          .green_from_blue = state.rgb_channel_mixer.green_from_blue,
          .blue_from_red = state.rgb_channel_mixer.blue_from_red,
          .blue_from_green = state.rgb_channel_mixer.blue_from_green,
          .blue_from_blue = state.rgb_channel_mixer.blue_from_blue,
      },
      .black_and_white_enabled = state.black_and_white_enabled,
      .black_and_white_mixer = state.black_and_white_mixer,
      .color_replacement_enabled = state.color_replacement_enabled,
      .color_replacement_source_hue = state.color_replacement_source_hue,
      .color_replacement_target_hue = state.color_replacement_target_hue,
      .color_replacement_range = state.color_replacement_range,
      .color_replacement_strength = state.color_replacement_strength,
      .skin_tone_protection_enabled = state.skin_tone_protection_enabled,
      .skin_tone_line_hue = state.skin_tone_line_hue,
      .skin_tone_protection_range = state.skin_tone_protection_range,
      .skin_tone_protection_strength = state.skin_tone_protection_strength,
      .color_harmony_enabled = state.color_harmony_enabled,
      .color_harmony_mode = state.color_harmony_mode.toStdString(),
      .color_harmony_base_hue = state.color_harmony_base_hue,
      .color_harmony_strength = state.color_harmony_strength,
      .grading_shadows_hue = state.grading_shadows_hue,
      .grading_shadows_saturation = state.grading_shadows_saturation,
      .grading_shadows_luminance = state.grading_shadows_luminance,
      .grading_midtones_hue = state.grading_midtones_hue,
      .grading_midtones_saturation = state.grading_midtones_saturation,
      .grading_midtones_luminance = state.grading_midtones_luminance,
      .grading_highlights_hue = state.grading_highlights_hue,
      .grading_highlights_saturation = state.grading_highlights_saturation,
      .grading_highlights_luminance = state.grading_highlights_luminance,
      .linear_mask_enabled = state.linear_mask_enabled,
      .linear_mask_exposure = state.linear_mask_exposure,
      .linear_mask_temperature = state.linear_mask_temperature,
      .linear_mask_feather = state.linear_mask_feather,
      .linear_mask_position = state.linear_mask_position,
      .linear_mask_rotation = state.linear_mask_rotation,
      .radial_mask_enabled = state.radial_mask_enabled,
      .radial_mask_exposure = state.radial_mask_exposure,
      .radial_mask_temperature = state.radial_mask_temperature,
      .radial_mask_feather = state.radial_mask_feather,
      .radial_mask_center_x = state.radial_mask_center_x,
      .radial_mask_center_y = state.radial_mask_center_y,
      .radial_mask_radius_x = state.radial_mask_radius_x,
      .radial_mask_radius_y = state.radial_mask_radius_y,
      .radial_mask_rotation = state.radial_mask_rotation,
      .local_mask_sets = pipeline_local_mask_sets(state.mask_stack),
      .retouch_masks = pipeline_retouch_masks(state.mask_stack),
      .photo_layers = pipeline_photo_layers(state.layers),
  };
}

void reduce_pipeline_state_for_draft_preview(
    photo_editor::pipeline::ColorAdjustmentState* state) {
  if (state == nullptr) {
    return;
  }

  state->texture = 0.0;
  state->clarity = 0.0;
  state->dehaze = 0.0;
  state->sharpening = 0.0;
  state->sharpen_masking = 0.0;
  state->luminance_nr = 0.0;
  state->color_nr = 0.0;
  state->lens_distortion = 0.0;
  state->lens_vignette = 0.0;
  state->chromatic_aberration = 0.0;
  state->linear_mask_enabled = false;
  state->radial_mask_enabled = false;
  state->local_mask_sets.clear();
  state->local_masks.clear();
  state->retouch_masks.clear();
}

void apply_matched_lens_profile(
    EditState* state,
    const QString& camera_label,
    const QString& lens_model) {
  if (state == nullptr || !state->raw_lens_correction) {
    return;
  }

  const auto* profile = photo_editor::pipeline::find_lens_profile(
      camera_label.toStdString(),
      lens_model.toStdString());
  if (profile == nullptr) {
    return;
  }

  state->lens_distortion = profile->distortion;
  state->lens_vignette = profile->vignette;
  state->chromatic_aberration = profile->chromatic_aberration;
}

double channel_weight_for_selection(double hue, const QString& hsl_channel) {
  return photo_editor::pipeline::channel_weight_for_selection(hue, hsl_channel.toStdString());
}

double guided_upright_perspective(double guide_angle) {
  return qBound(-100.0, -guide_angle * 2.5, 100.0);
}

double horizon_angle_degrees(double start_x, double start_y, double end_x, double end_y) {
  auto dx = end_x - start_x;
  auto dy = end_y - start_y;
  if (!std::isfinite(dx) || !std::isfinite(dy) || std::hypot(dx, dy) <= 1e-6) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  if (dx < 0.0) {
    dx = -dx;
    dy = -dy;
  }
  auto angle = qRadiansToDegrees(std::atan2(dy, dx));
  while (angle <= -90.0) {
    angle += 180.0;
  }
  while (angle > 90.0) {
    angle -= 180.0;
  }
  return angle;
}

QStringList built_in_crop_aspect_presets() {
  return {
      QStringLiteral("Original"),
      QStringLiteral("1:1"),
      QStringLiteral("4:5"),
      QStringLiteral("5:4"),
      QStringLiteral("3:2"),
      QStringLiteral("2:3"),
      QStringLiteral("4:3"),
      QStringLiteral("3:4"),
      QStringLiteral("16:9"),
      QStringLiteral("9:16"),
      QStringLiteral("4x6 in"),
      QStringLiteral("6x4 in"),
      QStringLiteral("5x7 in"),
      QStringLiteral("7x5 in"),
      QStringLiteral("8x10 in"),
      QStringLiteral("10x8 in"),
      QStringLiteral("11x14 in"),
      QStringLiteral("14x11 in"),
      QStringLiteral("A4"),
      QStringLiteral("A4 Landscape"),
  };
}

double crop_aspect_ratio(const QString& preset) {
  auto normalized = preset.trimmed().toLower();
  normalized.replace(QLatin1Char(':'), QLatin1Char('x'));
  normalized.remove(QLatin1Char(' '));
  if (normalized == QStringLiteral("square") || normalized == QStringLiteral("1x1")) {
    return 1.0;
  }
  if (normalized == QStringLiteral("a3") || normalized == QStringLiteral("a4") ||
      normalized == QStringLiteral("a5")) {
    return 210.0 / 297.0;
  }
  if (normalized == QStringLiteral("a3landscape") ||
      normalized == QStringLiteral("a4landscape") ||
      normalized == QStringLiteral("a5landscape")) {
    return 297.0 / 210.0;
  }
  const auto parts = normalized.split(QLatin1Char('x'), Qt::SkipEmptyParts);
  if (parts.size() == 2) {
    bool width_ok = false;
    bool height_ok = false;
    auto width_label = parts.at(0);
    auto height_label = parts.at(1);
    for (const auto& unit : {QStringLiteral("inches"), QStringLiteral("inch"), QStringLiteral("in"),
                             QStringLiteral("cm"), QStringLiteral("mm")}) {
      width_label.remove(unit);
      height_label.remove(unit);
    }
    const auto width = width_label.toDouble(&width_ok);
    const auto height = height_label.toDouble(&height_ok);
    if (width_ok && height_ok && width > 0.0 && height > 0.0) {
      return width / height;
    }
  }
  return 0.0;
}

QString normalized_custom_crop_preset(const QString& preset) {
  auto normalized = preset.simplified();
  normalized.replace(QLatin1Char('x'), QLatin1Char(':'), Qt::CaseInsensitive);
  if (crop_aspect_ratio(normalized) <= 0.0) {
    return {};
  }
  return normalized;
}

bool has_explicit_crop_rect(const EditState& state) {
  return state.crop_rect_width > 0.0 && state.crop_rect_height > 0.0;
}

QRect legacy_crop_rect_for_image(const QSize& size, const QString& preset, double crop_zoom) {
  int crop_width = size.width();
  int crop_height = size.height();

  const auto aspect_ratio = crop_aspect_ratio(preset);
  if (aspect_ratio > 0.0) {
    const auto current_ratio = static_cast<double>(size.width()) / static_cast<double>(size.height());
    if (current_ratio > aspect_ratio) {
      crop_width = static_cast<int>(qRound(static_cast<double>(size.height()) * aspect_ratio));
    } else {
      crop_height = static_cast<int>(qRound(static_cast<double>(size.width()) / aspect_ratio));
    }
  }

  crop_width = qMax(1, static_cast<int>(qRound(static_cast<double>(crop_width) / crop_zoom)));
  crop_height = qMax(1, static_cast<int>(qRound(static_cast<double>(crop_height) / crop_zoom)));

  const auto x = qMax(0, (size.width() - crop_width) / 2);
  const auto y = qMax(0, (size.height() - crop_height) / 2);
  return {x, y, crop_width, crop_height};
}

QRectF normalized_crop_rect_for_state(const QSize& size, const EditState& state) {
  if (!size.isValid() || size.width() <= 0 || size.height() <= 0) {
    return QRectF(0.0, 0.0, 1.0, 1.0);
  }

  if (has_explicit_crop_rect(state)) {
    const auto width = clamp_unit(state.crop_rect_width);
    const auto height = clamp_unit(state.crop_rect_height);
    const auto x = clamp_unit(state.crop_rect_x);
    const auto y = clamp_unit(state.crop_rect_y);
    return QRectF(
        std::min(x, std::max(0.0, 1.0 - width)),
        std::min(y, std::max(0.0, 1.0 - height)),
        width,
        height);
  }

  const auto legacy = legacy_crop_rect_for_image(size, state.crop_preset, state.crop_zoom);
  return QRectF(
      static_cast<double>(legacy.x()) / static_cast<double>(size.width()),
      static_cast<double>(legacy.y()) / static_cast<double>(size.height()),
      static_cast<double>(legacy.width()) / static_cast<double>(size.width()),
      static_cast<double>(legacy.height()) / static_cast<double>(size.height()));
}

QRect crop_rect_for_image(const QSize& size, const EditState& state) {
  if (!size.isValid()) {
    return {};
  }

  const auto normalized = normalized_crop_rect_for_state(size, state);
  const auto x = qBound(0, static_cast<int>(qRound(normalized.x() * static_cast<double>(size.width()))), size.width() - 1);
  const auto y = qBound(0, static_cast<int>(qRound(normalized.y() * static_cast<double>(size.height()))), size.height() - 1);
  const auto width = qBound(1, static_cast<int>(qRound(normalized.width() * static_cast<double>(size.width()))), size.width() - x);
  const auto height = qBound(1, static_cast<int>(qRound(normalized.height() * static_cast<double>(size.height()))), size.height() - y);
  return {x, y, width, height};
}

bool has_visible_adjustments(const EditState& state) {
  return !qFuzzyIsNull(state.raw_exposure) ||
         !qFuzzyIsNull(state.raw_boost) ||
         !qFuzzyCompare(state.raw_shadow_boost, 1.0) ||
         !qFuzzyIsNull(state.raw_detail) ||
         !qFuzzyIsNull(state.raw_chroma_noise_reduction) ||
         !qFuzzyIsNull(state.raw_moire_reduction) ||
         !qFuzzyIsNull(state.raw_hot_dead_pixel_cleanup) ||
         !qFuzzyIsNull(state.raw_false_color_suppression) ||
         !state.raw_highlight_recovery ||
         normalized_raw_demosaic_quality_name(state.raw_demosaic_quality) != QStringLiteral("Balanced") ||
         !state.raw_gamut_mapping ||
         !state.raw_lens_correction ||
         state.raw_white_balance_override ||
         normalized_camera_matching_look_name(state.raw_camera_matching_look) != QStringLiteral("Standard") ||
         !qFuzzyCompare(state.raw_white_balance_temperature, 5200.0) ||
         !qFuzzyIsNull(state.raw_white_balance_tint) ||
         !qFuzzyIsNull(state.raw_black_level) ||
         !qFuzzyCompare(state.raw_white_level, 1.0) ||
         !qFuzzyIsNull(state.raw_calibration_hue) ||
         !qFuzzyIsNull(state.raw_calibration_saturation) ||
         !qFuzzyIsNull(state.raw_red_primary_hue) ||
         !qFuzzyIsNull(state.raw_red_primary_saturation) ||
         !qFuzzyIsNull(state.raw_green_primary_hue) ||
         !qFuzzyIsNull(state.raw_green_primary_saturation) ||
         !qFuzzyIsNull(state.raw_blue_primary_hue) ||
         !qFuzzyIsNull(state.raw_blue_primary_saturation) ||
         !qFuzzyIsNull(state.exposure) ||
         !qFuzzyIsNull(state.contrast) ||
         !qFuzzyIsNull(state.highlights) ||
         !qFuzzyIsNull(state.shadows) ||
         !qFuzzyIsNull(state.whites) ||
         !qFuzzyIsNull(state.blacks) ||
         !qFuzzyIsNull(state.brightness) ||
         !qFuzzyIsNull(state.hdr_local_tone_mapping) ||
         !qFuzzyIsNull(state.filmic_shoulder) ||
         !qFuzzyIsNull(state.filmic_toe) ||
         !qFuzzyIsNull(state.adaptive_contrast) ||
         !qFuzzyIsNull(state.levels_black_point) ||
         !qFuzzyIsNull(state.levels_gamma) ||
         !qFuzzyIsNull(state.levels_white_point) ||
         !qFuzzyIsNull(state.zone_deep_shadows) ||
         !qFuzzyIsNull(state.zone_shadows) ||
         !qFuzzyIsNull(state.zone_midtones) ||
         !qFuzzyIsNull(state.zone_lights) ||
         !qFuzzyIsNull(state.zone_highlights) ||
         !qFuzzyIsNull(state.temperature) ||
         !qFuzzyIsNull(state.tint) ||
         !qFuzzyIsNull(state.texture) ||
         !qFuzzyIsNull(state.clarity) ||
         !qFuzzyIsNull(state.dehaze) ||
         !qFuzzyIsNull(state.vibrance) ||
         !qFuzzyIsNull(state.saturation) ||
         !qFuzzyIsNull(state.lens_distortion) ||
         !qFuzzyIsNull(state.lens_vignette) ||
         !qFuzzyIsNull(state.chromatic_aberration) ||
         !qFuzzyIsNull(state.curve_shadows) ||
         !qFuzzyIsNull(state.curve_darks) ||
         !qFuzzyIsNull(state.curve_lights) ||
         !qFuzzyIsNull(state.curve_highlights) ||
         !state.master_curve_points.isEmpty() ||
         !state.luma_curve_points.isEmpty() ||
         !state.red_curve_points.isEmpty() ||
         !state.green_curve_points.isEmpty() ||
         !state.blue_curve_points.isEmpty() ||
         !qFuzzyIsNull(state.hue_adjustment) ||
         !qFuzzyIsNull(state.saturation_adjustment) ||
         !qFuzzyIsNull(state.luminance_adjustment) ||
         has_explicit_crop_rect(state) ||
         !qFuzzyCompare(state.crop_zoom, 1.0) ||
          state.crop_preset != QStringLiteral("Original") ||
          !state.mask_stack.isEmpty() ||
          !state.layers.isEmpty() ||
          state.linear_mask_enabled ||
         !qFuzzyIsNull(state.linear_mask_exposure) ||
         !qFuzzyIsNull(state.linear_mask_temperature) ||
         !qFuzzyCompare(state.linear_mask_feather, 0.22) ||
         !qFuzzyCompare(state.linear_mask_position, 0.5) ||
         !qFuzzyIsNull(state.linear_mask_rotation) ||
         state.radial_mask_enabled ||
         !qFuzzyIsNull(state.radial_mask_exposure) ||
         !qFuzzyIsNull(state.radial_mask_temperature) ||
         !qFuzzyCompare(state.radial_mask_feather, 0.25) ||
         !qFuzzyCompare(state.radial_mask_center_x, 0.5) ||
         !qFuzzyCompare(state.radial_mask_center_y, 0.5) ||
         !qFuzzyCompare(state.radial_mask_radius_x, 0.25) ||
         !qFuzzyCompare(state.radial_mask_radius_y, 0.25) ||
         !qFuzzyIsNull(state.radial_mask_rotation);
}

bool requires_full_preview_pipeline(const EditState& state) {
  return std::abs(state.texture) > 1e-6 || std::abs(state.clarity) > 1e-6 || std::abs(state.dehaze) > 1e-6 ||
         std::abs(state.hdr_local_tone_mapping) > 1e-6 ||
         std::abs(state.sharpening) > 1e-6 || std::abs(state.sharpen_masking) > 1e-6 ||
         std::abs(state.luminance_nr) > 1e-6 || std::abs(state.color_nr) > 1e-6 ||
         std::abs(state.lens_distortion) > 1e-6 || std::abs(state.lens_vignette) > 1e-6 ||
         std::abs(state.chromatic_aberration) > 1e-6;
}

bool requires_cpu_only_preview_pipeline(const EditState& state) {
  return std::abs(state.lens_distortion) > 1e-6 ||
         std::abs(state.lens_vignette) > 1e-6 ||
         std::abs(state.chromatic_aberration) > 1e-6 ||
         std::abs(state.hdr_local_tone_mapping) > 1e-6 ||
         !state.layers.isEmpty();
}

bool has_extended_range_pixels(const std::vector<photo_editor::pipeline::ColorPixel>& pixels) {
  constexpr auto kTolerance = 1.0e-6F;
  return std::any_of(pixels.cbegin(), pixels.cend(), [](const auto& pixel) {
    return pixel.red > 1.0F + kTolerance || pixel.green > 1.0F + kTolerance ||
           pixel.blue > 1.0F + kTolerance || pixel.red < -kTolerance ||
           pixel.green < -kTolerance || pixel.blue < -kTolerance;
  });
}

QString preview_gpu_fallback_reason(
    const EditState& state,
    const photo_editor::pipeline::ColorAdjustmentState& pipeline_state,
    const std::vector<photo_editor::pipeline::ColorPixel>& pixels,
    bool metal_available) {
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  if (!metal_available) {
    return QStringLiteral("metal_unavailable");
  }
  if (!pipeline_state.local_mask_sets.empty()) {
    return QStringLiteral("local_mask_sets");
  }
  if (!pipeline_state.retouch_masks.empty()) {
    return QStringLiteral("retouch_masks");
  }
  if (!pipeline_state.local_masks.empty()) {
    return QStringLiteral("legacy_local_masks");
  }
  if (has_extended_range_pixels(pixels)) {
    return QStringLiteral("extended_range_pixels");
  }
  if (requires_full_preview_pipeline(state)) {
    return QStringLiteral("full_pipeline_required");
  }
  return {};
#else
  Q_UNUSED(state);
  Q_UNUSED(pipeline_state);
  Q_UNUSED(pixels);
  Q_UNUSED(metal_available);
  return QStringLiteral("metal_not_compiled");
#endif
}

bool has_rendered_local_masks(const EditState& state) {
  auto ordinal = 1;
  for (const auto& value : state.mask_stack) {
    if (mask_definition_affects_render(value.toMap(), ordinal)) {
      return true;
    }
    ++ordinal;
  }
  return false;
}

bool uses_expensive_interactive_preview(const EditState& state) {
  return has_rendered_local_masks(state) || requires_cpu_only_preview_pipeline(state);
}

int interactive_preview_edge_for_state(const EditState& state, int requested_edge) {
  const auto clamped_requested = clamp_preview_target_edge(requested_edge);
  if (!uses_expensive_interactive_preview(state)) {
    return clamped_requested;
  }
  return std::min(clamped_requested, kExpensiveInteractivePreviewEdge);
}

int draft_interactive_preview_edge_for_state(const EditState& state, int requested_edge) {
  return std::min(
      interactive_preview_edge_for_state(state, requested_edge),
      uses_expensive_interactive_preview(state) ? kExpensiveDraftInteractivePreviewEdge
                                                : kDraftInteractivePreviewEdge);
}

int interactive_render_debounce_ms_for_state(const EditState& state) {
  return uses_expensive_interactive_preview(state) ? kExpensiveInteractiveRenderDebounceMs : 20;
}

int full_render_debounce_ms_for_state(const EditState& state) {
  return uses_expensive_interactive_preview(state) ? kExpensiveFullRenderDebounceMs : kFullRenderDebounceMs;
}

EditState reference_preview_cache_state(const EditState& state) {
  EditState cached_state;
  cached_state.input_color_profile = state.input_color_profile;
  cached_state.working_color_profile = state.working_color_profile;
  cached_state.preview_color_space = state.preview_color_space;
  cached_state.render_intent = state.render_intent;
  cached_state.raw_input_transform_enabled = state.raw_input_transform_enabled;
  cached_state.highlight_reconstruction_mode = state.highlight_reconstruction_mode;
  cached_state.chromatic_adaptation_method = state.chromatic_adaptation_method;
  cached_state.raw_exposure = state.raw_exposure;
  cached_state.raw_boost = state.raw_boost;
  cached_state.raw_shadow_boost = state.raw_shadow_boost;
  cached_state.raw_detail = state.raw_detail;
  cached_state.raw_chroma_noise_reduction = state.raw_chroma_noise_reduction;
  cached_state.raw_moire_reduction = state.raw_moire_reduction;
  cached_state.raw_hot_dead_pixel_cleanup = state.raw_hot_dead_pixel_cleanup;
  cached_state.raw_false_color_suppression = state.raw_false_color_suppression;
  cached_state.raw_highlight_recovery = state.raw_highlight_recovery;
  cached_state.raw_demosaic_quality = state.raw_demosaic_quality;
  cached_state.raw_gamut_mapping = state.raw_gamut_mapping;
  cached_state.raw_lens_correction = state.raw_lens_correction;
  cached_state.raw_white_balance_override = state.raw_white_balance_override;
  cached_state.raw_camera_matching_look = state.raw_camera_matching_look;
  cached_state.raw_white_balance_temperature = state.raw_white_balance_temperature;
  cached_state.raw_white_balance_tint = state.raw_white_balance_tint;
  cached_state.raw_black_level = state.raw_black_level;
  cached_state.raw_white_level = state.raw_white_level;
  cached_state.raw_calibration_hue = state.raw_calibration_hue;
  cached_state.raw_calibration_saturation = state.raw_calibration_saturation;
  cached_state.raw_red_primary_hue = state.raw_red_primary_hue;
  cached_state.raw_red_primary_saturation = state.raw_red_primary_saturation;
  cached_state.raw_green_primary_hue = state.raw_green_primary_hue;
  cached_state.raw_green_primary_saturation = state.raw_green_primary_saturation;
  cached_state.raw_blue_primary_hue = state.raw_blue_primary_hue;
  cached_state.raw_blue_primary_saturation = state.raw_blue_primary_saturation;
  cached_state.crop_rect_x = state.crop_rect_x;
  cached_state.crop_rect_y = state.crop_rect_y;
  cached_state.crop_rect_width = state.crop_rect_width;
  cached_state.crop_rect_height = state.crop_rect_height;
  cached_state.crop_zoom = state.crop_zoom;
  cached_state.crop_preset = state.crop_preset;
  cached_state.output_color_space = state.output_color_space;
  return cached_state;
}

EditState base_linear_cache_state(const EditState& state, bool bypass_crop) {
  auto cached_state = reference_preview_cache_state(state);
  cached_state.output_color_space = QStringLiteral("sRGB");
  if (!bypass_crop) {
    return cached_state;
  }

  cached_state.crop_rect_x = 0.0;
  cached_state.crop_rect_y = 0.0;
  cached_state.crop_rect_width = 0.0;
  cached_state.crop_rect_height = 0.0;
  cached_state.crop_zoom = 1.0;
  cached_state.crop_preset = QStringLiteral("Original");
  return cached_state;
}

QString base_linear_cache_key(const RenderRequest& request) {
  if (request.full_resolution || request.max_edge <= 0 || request.file_path.isEmpty() ||
      request.preview_overlay_rect.isValid()) {
    return {};
  }

  const QFileInfo source_info(request.file_path);
  if (!source_info.exists()) {
    return {};
  }

  const auto fingerprint = QStringLiteral("%1|%2|%3|%4")
                               .arg(decoded_image_cache_key(source_info, request.max_edge))
                               .arg(QStringLiteral("linear-base-v1"))
                               .arg(request.bypass_crop ? QStringLiteral("uncropped")
                                                        : QStringLiteral("cropped"))
                               .arg(serialize_edit_recipe_json(
                                   base_linear_cache_state(request.state, request.bypass_crop)));
  const auto digest =
      QCryptographicHash::hash(fingerprint.toUtf8(), QCryptographicHash::Sha1).toHex();
  return QString::fromLatin1(digest);
}

std::shared_ptr<LinearImageBuffer> linearize_preview_image(
    const QImage& image,
    const RenderRequest* request) {
  if (image.isNull()) {
    return {};
  }

  const auto linear_qimage = working_image_qimage(image);
  if (linear_qimage.isNull()) {
    return {};
  }

  auto linear_image = std::make_shared<LinearImageBuffer>();
  linear_image->width = linear_qimage.width();
  linear_image->height = linear_qimage.height();
  linear_image->pixels.resize(
      static_cast<std::size_t>(linear_image->width * linear_image->height));

  for (int y = 0; y < linear_image->height; ++y) {
    if (request != nullptr && render_request_superseded(*request)) {
      return {};
    }
    const auto* scanline =
        reinterpret_cast<const QRgbaFloat16*>(linear_qimage.constScanLine(y));
    for (int x = 0; x < linear_image->width; ++x) {
      const auto& pixel = scanline[x];
      linear_image->pixels[static_cast<std::size_t>(y * linear_image->width + x)] =
          photo_editor::pipeline::ColorPixel{
              .red = std::max(0.0F, static_cast<float>(pixel.r)),
              .green = std::max(0.0F, static_cast<float>(pixel.g)),
              .blue = std::max(0.0F, static_cast<float>(pixel.b)),
              .alpha = static_cast<float>(clamp_unit(static_cast<double>(pixel.a))),
          };
    }
  }

  return linear_image;
}

std::shared_ptr<LinearImageBuffer> decode_linear_source_image(const RenderRequest& request) {
  if (request.file_path.isEmpty() || request.preview_overlay_rect.isValid()) {
    return {};
  }

  const QFileInfo source_info(request.file_path);
  if (!source_info.exists() || !source_info.isReadable()) {
    return {};
  }

  QImage decoded;
  if (native_image_supported(request.file_path)) {
    if (!decode_native_linear_image(request.file_path, &decoded, request.max_edge)) {
      return {};
    }
  } else {
    QImageReader reader(request.file_path);
    reader.setAutoTransform(true);
    if (reader.canRead()) {
      if (request.max_edge > 0) {
        const auto source_size = reader.size();
        if (source_size.isValid()) {
          const auto target_size = scaled_size_for_max_edge(source_size, request.max_edge);
          if (target_size.isValid() && target_size != source_size) {
            reader.setScaledSize(target_size);
          }
        }
      }
      decoded = reader.read();
      if (decoded.isNull()) {
        return {};
      }
      if (!decoded.colorSpace().isValid()) {
        decoded.setColorSpace(preview_target_color_space());
      }
    } else if (!decode_raw_linear_image(
                   request.file_path,
                   &decoded,
                   request.max_edge,
                   raw_decode_settings_for_state(request.state))) {
      return {};
    }
  }

  auto linear_image = linearize_preview_image(decoded, &request);
  if (!linear_image || render_request_superseded(request)) {
    return {};
  }

  if (!request.bypass_crop) {
    *linear_image = crop_working_image(
        *linear_image,
        crop_rect_for_image(QSize(linear_image->width, linear_image->height), request.state));
    if (linear_image->isNull()) {
      return {};
    }
  }

  return linear_image;
}

std::shared_ptr<LinearImageBuffer> cached_linear_preview_image(
    const QImage& image,
    const RenderRequest* request,
    PreviewAdjustmentTelemetry* telemetry) {
  const auto cache_key = request == nullptr ? QString{} : base_linear_cache_key(*request);
  if (telemetry != nullptr) {
    telemetry->linear_cache_lookup = !cache_key.isEmpty();
    telemetry->linear_cache_hit = false;
    telemetry->linear_cache = linear_image_cache_snapshot();
  }
  if (cache_key.isEmpty()) {
    return linearize_preview_image(image, request);
  }

  {
    auto& cache = linear_image_cache();
    QMutexLocker locker(&cache.mutex);
    const auto cached = cache.images.constFind(cache_key);
    if (cached != cache.images.cend()) {
      cache.touch(cache_key);
      if (telemetry != nullptr) {
        telemetry->linear_cache_hit = true;
        telemetry->linear_cache = CacheSnapshot{
            .entries = static_cast<int>(cache.images.size()),
            .bytes = cache.total_bytes,
        };
      }
      return cached.value();
    }
  }

  auto linear_image = request != nullptr ? decode_linear_source_image(*request)
                                         : linearize_preview_image(image, request);
  if (!linear_image || (request != nullptr && render_request_superseded(*request))) {
    return {};
  }

  auto& cache = linear_image_cache();
  QMutexLocker locker(&cache.mutex);
  cache.insert(cache_key, linear_image);
  if (telemetry != nullptr) {
    telemetry->linear_cache = CacheSnapshot{
        .entries = static_cast<int>(cache.images.size()),
        .bytes = cache.total_bytes,
    };
  }
  return linear_image;
}

bool decode_source_image(
    const QString& file_path,
    QImage* image,
    int max_edge,
    const EditState& state = {},
    DecodeSourceTelemetry* telemetry = nullptr) {
  if (image == nullptr || !preview_supported(file_path)) {
    return false;
  }

  const QFileInfo source_info(file_path);
  if (!source_info.exists() || !source_info.isReadable()) {
    return false;
  }

  const auto native_supported = native_image_supported(file_path);
  const auto qt_supported =
      !native_supported &&
      QImageReader::supportedImageFormats().contains(source_info.suffix().toLower().toUtf8());
  const auto should_cache = max_edge > 0;
  auto cache_key = should_cache ? decoded_image_cache_key(source_info, max_edge) : QString{};
  if (should_cache && !qt_supported) {
    cache_key += QStringLiteral("|raw|") + raw_decode_settings_key(state);
  }
  if (telemetry != nullptr) {
    telemetry->cache_lookup = should_cache;
    telemetry->cache_hit = false;
    telemetry->decoded_cache = decoded_image_cache_snapshot();
    telemetry->backend = native_supported ? QStringLiteral("native")
                       : qt_supported ? QStringLiteral("qt")
                                       : QStringLiteral("raw");
  }
  if (should_cache) {
    auto cache_snapshot = CacheSnapshot{};
    if (find_decoded_image_cache_entry(cache_key, image, &cache_snapshot)) {
      if (telemetry != nullptr) {
        telemetry->cache_hit = true;
        telemetry->decoded_cache = cache_snapshot;
      }
      return !image->isNull();
    }
    if (telemetry != nullptr) {
      telemetry->decoded_cache = cache_snapshot;
    }
  }

  if (native_supported) {
    auto decoded = QImage{};
    if (!decode_native_image(file_path, &decoded, max_edge)) {
      return false;
    }

    *image = normalize_preview_color_space(std::move(decoded));
    if (should_cache && !image->isNull()) {
      auto cache_snapshot = CacheSnapshot{};
      insert_decoded_image_cache_entry(cache_key, *image, &cache_snapshot);
      if (telemetry != nullptr) {
        telemetry->decoded_cache = cache_snapshot;
      }
    }
    return !image->isNull();
  }

  if (!qt_supported) {
    auto decoded = QImage{};
    if (!decode_raw_image(file_path, &decoded, max_edge, raw_decode_settings_for_state(state))) {
      return false;
    }

    *image = normalize_preview_color_space(std::move(decoded));
    if (should_cache && !image->isNull()) {
      auto cache_snapshot = CacheSnapshot{};
      insert_decoded_image_cache_entry(cache_key, *image, &cache_snapshot);
      if (telemetry != nullptr) {
        telemetry->decoded_cache = cache_snapshot;
      }
    }
    return !image->isNull();
  }

  QImageReader reader(file_path);
  reader.setAutoTransform(true);
  if (!reader.canRead()) {
    return false;
  }

  if (max_edge > 0) {
    const auto source_size = reader.size();
    if (source_size.isValid()) {
      const auto target_size = scaled_size_for_max_edge(source_size, max_edge);
      if (target_size.isValid() && target_size != source_size) {
        reader.setScaledSize(target_size);
      }
    }
  }

  auto decoded = reader.read();
  if (decoded.isNull()) {
    return false;
  }

  *image = normalize_preview_color_space(std::move(decoded));
  if (should_cache && !image->isNull()) {
    auto cache_snapshot = CacheSnapshot{};
    insert_decoded_image_cache_entry(cache_key, *image, &cache_snapshot);
    if (telemetry != nullptr) {
      telemetry->decoded_cache = cache_snapshot;
    }
  }
  return !image->isNull();
}

SmartSelectPreviewResult compute_smart_select_preview_result(
    const SmartSelectPreviewRequest& request) {
  SmartSelectPreviewResult result;
  result.generation = request.generation;
  if (request.file_path.isEmpty() || request.points.isEmpty()) {
    return result;
  }

  QImage source_image;
  if (!decode_source_image(request.file_path, &source_image, 1024, request.state)) {
    return result;
  }

  const auto normalized_points = normalized_smart_select_path(request.points);
  const auto segmentation_result = compute_smart_select_result(source_image, request.points);
  if (!segmentation_result.success) {
    return result;
  }

  result.preview_mask =
      segmentation_mask_variant(segmentation_result, QStringLiteral("object"));
  result.success = !result.preview_mask.isEmpty();
  return result;
}

QImage apply_crop(const QImage& image, const EditState& state) {
  return image.copy(crop_rect_for_image(image.size(), state));
}

bool apply_color_adjustments(
    QImage* image,
    const EditState& state,
    const RenderRequest* request = nullptr,
    PreviewAdjustmentTelemetry* telemetry = nullptr);

QRectF source_overlay_rect_for_request(const QSize& source_size, const RenderRequest& request) {
  auto overlay_rect = request.preview_overlay_rect.intersected(QRectF(0.0, 0.0, 1.0, 1.0));
  if (!overlay_rect.isValid() || overlay_rect.width() <= 0.0 || overlay_rect.height() <= 0.0) {
    return {};
  }

  if (request.bypass_crop) {
    return overlay_rect;
  }

  const auto crop_rect = normalized_crop_rect_for_state(source_size, request.state);
  return QRectF(
      crop_rect.x() + overlay_rect.x() * crop_rect.width(),
      crop_rect.y() + overlay_rect.y() * crop_rect.height(),
      crop_rect.width() * overlay_rect.width(),
      crop_rect.height() * overlay_rect.height())
      .intersected(QRectF(0.0, 0.0, 1.0, 1.0));
}

bool decode_qt_source_image_region(
    const QString& file_path,
    const QRect& clip_rect,
    QImage* image) {
  if (image == nullptr || !clip_rect.isValid()) {
    return false;
  }

  QImageReader reader(file_path);
  reader.setAutoTransform(true);
  if (!reader.canRead()) {
    return false;
  }
  reader.setClipRect(clip_rect);
  const auto decoded = reader.read();
  if (decoded.isNull()) {
    return false;
  }

  *image = decoded;
  if (!image->colorSpace().isValid()) {
    image->setColorSpace(preview_target_color_space());
  }
  return !image->isNull();
}

QImage render_preview_overlay_patch(const RenderRequest& request, bool edited) {
  if (request.full_resolution || !request.use_in_memory_preview || request.compare_mode ||
      !request.preview_overlay_rect.isValid() || request.preview_overlay_max_edge <= 0) {
    return {};
  }

  const QFileInfo source_info(request.file_path);
  const auto native_supported = native_image_supported(request.file_path);
  const auto qt_supported =
      !native_supported &&
      QImageReader::supportedImageFormats().contains(source_info.suffix().toLower().toUtf8());
  if (!qt_supported) {
    return {};
  }

  const auto source_size = source_image_size_for_file(request.file_path);
  const auto source_overlay_rect =
      source_overlay_rect_for_request(source_size, request);
  const auto source_clip_rect =
      normalized_rect_to_image_rect(source_size, source_overlay_rect);
  if (!source_clip_rect.isValid()) {
    return {};
  }

  QImage overlay_image;
  if (!decode_qt_source_image_region(request.file_path, source_clip_rect, &overlay_image)) {
    return {};
  }
  if (render_request_superseded(request)) {
    return {};
  }

  if (edited && has_visible_adjustments(request.state) &&
      !apply_color_adjustments(&overlay_image, request.state, &request)) {
    return {};
  }
  apply_preview_display_transform(&overlay_image, request.state);

  const auto target_size =
      scaled_size_for_max_edge(source_clip_rect.size(), request.preview_overlay_max_edge);
  if (target_size.isValid() && overlay_image.size() != target_size) {
    overlay_image = overlay_image.scaled(
        target_size,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
  }
  return overlay_image;
}

#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
photo_editor::pipeline::metal::MetalPipelineBackend& metal_backend() {
  static photo_editor::pipeline::metal::MetalPipelineBackend backend;
  return backend;
}
#endif

bool apply_color_adjustments(
    QImage* image,
    const EditState& state,
    const RenderRequest* request,
    PreviewAdjustmentTelemetry* telemetry) {
  if (telemetry != nullptr) {
    *telemetry = {};
  }
  if (image == nullptr || image->isNull()) {
    if (telemetry != nullptr) {
      telemetry->backend = QStringLiteral("none");
      telemetry->fallback_reason = QStringLiteral("empty_source");
    }
    return false;
  }

  const auto linear_source = cached_linear_preview_image(*image, request, telemetry);
  if (!linear_source) {
    if (telemetry != nullptr) {
      telemetry->backend = QStringLiteral("none");
      telemetry->fallback_reason = QStringLiteral("linearization_failed");
    }
    return false;
  }

  auto pixels = linear_source->pixels;
  const auto width = linear_source->width;
  const auto height = linear_source->height;
  if (request != nullptr && render_request_superseded(*request)) {
    return false;
  }

  photo_editor::pipeline::DecodedImageMetadata decoded_metadata;
  const auto* metadata = request != nullptr &&
                                  read_raw_decoded_metadata(
                                      request->file_path,
                                      &decoded_metadata,
                                     raw_decode_settings_for_state(request->state))
                             ? &decoded_metadata
                             : nullptr;

#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  auto& metal = metal_backend();
  auto pipeline_state = to_pipeline_state(state, metadata);
  if (request != nullptr && request->draft_interactive) {
    reduce_pipeline_state_for_draft_preview(&pipeline_state);
  }
  // The reduced controller fast path only supports mask compositing plus the LUT stage.
  // Spatial tone/detail effects still go through the full engine pipeline, which can
  // selectively use Metal per stage.
  const auto fallback_reason =
      preview_gpu_fallback_reason(state, pipeline_state, pixels, metal.available());
  if (fallback_reason.isEmpty()) {
    if (telemetry != nullptr) {
      telemetry->backend = QStringLiteral("metal");
      telemetry->fallback_reason.clear();
    }
    if (request != nullptr && render_request_superseded(*request)) {
      return false;
    }
    const auto lut = photo_editor::pipeline::build_color_lut(pipeline_state);
    if (request != nullptr && render_request_superseded(*request)) {
      return false;
    }
    metal.apply_masks_and_color_lut(&pixels, width, height, pipeline_state, lut);
  } else {
    if (telemetry != nullptr) {
      telemetry->backend = QStringLiteral("cpu");
      telemetry->fallback_reason = fallback_reason;
    }
    photo_editor::pipeline::apply_color_adjustments(
        &pixels,
        width,
        height,
        pipeline_state,
        photo_editor::pipeline::RemovalQuality::Interactive,
        request != nullptr
            ? photo_editor::pipeline::CancellationToken{
                  request->latest_generation.get(),
                  request->generation,
              }
            : photo_editor::pipeline::CancellationToken{});
  }
#else
  auto pipeline_state = to_pipeline_state(state, metadata);
  if (request != nullptr && request->draft_interactive) {
    reduce_pipeline_state_for_draft_preview(&pipeline_state);
  }
  if (telemetry != nullptr) {
    telemetry->backend = QStringLiteral("cpu");
    telemetry->fallback_reason = QStringLiteral("metal_not_compiled");
  }
  photo_editor::pipeline::apply_color_adjustments(
      &pixels,
      width,
      height,
      pipeline_state,
      photo_editor::pipeline::RemovalQuality::Interactive,
      request != nullptr
          ? photo_editor::pipeline::CancellationToken{
                request->latest_generation.get(),
                request->generation,
            }
          : photo_editor::pipeline::CancellationToken{});
#endif

  if (request != nullptr && render_request_superseded(*request)) {
    return false;
  }

  *image = qimage_from_working_image(
      WorkingImage{
          .width = width,
          .height = height,
          .pixels = std::move(pixels),
      },
      output_color_space_for_name(preview_output_color_space_for_state(state)),
      QImage::Format_ARGB32,
      state.working_color_profile);
  return true;
}

QString cache_path_for_selection(
    const QString& file_path,
    const EditState& state,
    bool edited,
    bool full_resolution,
    int max_edge) {
  const auto cache_state = edited ? state : reference_preview_cache_state(state);
  const auto fingerprint = QStringLiteral("%1|%2|%3|%4|%5|%6")
                               .arg(file_path)
                               .arg(QStringLiteral("pipeline-v3"))
                               .arg(edited ? QStringLiteral("edited") : QStringLiteral("reference"))
                               .arg(full_resolution ? QStringLiteral("full") : QStringLiteral("interactive"))
                               .arg(max_edge)
                               .arg(serialize_edit_recipe_json(cache_state));
  const auto digest =
      QCryptographicHash::hash(fingerprint.toUtf8(), QCryptographicHash::Sha1).toHex();
  return QDir(preview_cache_root()).filePath(QStringLiteral("%1.jpg").arg(QString::fromLatin1(digest)));
}

QString cache_path_for_selection(
    const QString& file_path,
    const EditState& state,
    bool edited,
    bool full_resolution,
    bool bypass_crop,
    int max_edge) {
  return cache_path_for_selection(
      file_path + (bypass_crop ? QStringLiteral("|crop-editing") : QString()),
      state,
      edited,
      full_resolution,
      max_edge);
}

RenderResult render_preview_request(const RenderRequest& request) {
  QElapsedTimer total_timer;
  total_timer.start();
  RenderResult result;
  result.full_resolution = request.full_resolution;
  result.overlay_only = request.overlay_only;
  result.draft_interactive = request.draft_interactive;
  result.generation = request.generation;
  result.render_reason = request.render_reason;
  result.interaction_mode = request.interaction_mode;
  result.queue_wait_ms = request.queue_wait_ms;
  result.preview_overlay_rect = request.preview_overlay_rect;
  result.had_adjustments = has_visible_adjustments(request.state);
  result.backend = result.had_adjustments ? QStringLiteral("unknown") : QStringLiteral("none");

  if (request.overlay_only) {
    result.success = true;
    if (request.preview_overlay_rect.isValid() && request.preview_overlay_max_edge > 0) {
      result.preview_overlay_image = render_preview_overlay_patch(request, true);
      if (!result.preview_overlay_image.isNull()) {
        result.display_preview_overlay_source =
            QStringLiteral("image://editor-preview/%1")
                .arg(QString::fromLatin1(kEditedPreviewOverlayImageId));
      }
    }
    return result;
  }

  QImage source_image;
  DecodeSourceTelemetry decode_telemetry;
  QElapsedTimer decode_timer;
  decode_timer.start();
  if (!decode_source_image(
          request.file_path,
          &source_image,
          request.max_edge,
          request.state,
          &decode_telemetry)) {
    result.status = QStringLiteral("Source file unavailable.");
    result.total_elapsed_ms = total_timer.elapsed();
    result.decode_elapsed_ms = decode_timer.elapsed();
    observability::logWarning(
        photoEditorPreviewLog(),
        QStringLiteral("preview.render.failed"),
        {
            {QStringLiteral("stage"), request.full_resolution ? QStringLiteral("full") : QStringLiteral("interactive")},
            {QStringLiteral("generation"), request.generation},
            {QStringLiteral("render_reason"), request.render_reason},
            {QStringLiteral("interaction_mode"), request.interaction_mode},
            {QStringLiteral("file_path"), request.file_path},
            {QStringLiteral("elapsed_ms"), result.total_elapsed_ms},
            {QStringLiteral("status"), result.status},
        });
    return result;
  }
  result.decode_elapsed_ms = decode_timer.elapsed();
  result.decoded_cache_lookup = decode_telemetry.cache_lookup;
  result.decoded_cache_hit = decode_telemetry.cache_hit;
  result.decode_backend = decode_telemetry.backend;
  result.decoded_cache_entries = decode_telemetry.decoded_cache.entries;
  result.decoded_cache_bytes = decode_telemetry.decoded_cache.bytes;
  result.source_width = source_image.width();
  result.source_height = source_image.height();
  if (raw_image_supported(request.file_path)) {
    const auto raw_clipping = raw_clipping_stats_for_image(source_image);
    result.raw_clip_sample_count = raw_clipping.sample_count;
    result.raw_shadow_clip_count = raw_clipping.shadow_clip_count;
    result.raw_highlight_clip_count = raw_clipping.highlight_clip_count;
  }
  if (render_request_superseded(request)) {
    result.cancelled = true;
    result.total_elapsed_ms = total_timer.elapsed();
    return result;
  }

  QDir().mkpath(preview_cache_root());

  QElapsedTimer edit_timer;
  edit_timer.start();
  QImage edited_image = request.bypass_crop ? source_image : apply_crop(source_image, request.state);
  if (render_request_superseded(request)) {
    result.cancelled = true;
    result.total_elapsed_ms = total_timer.elapsed();
    result.edit_elapsed_ms = edit_timer.elapsed();
    return result;
  }
  if (has_visible_adjustments(request.state)) {
    PreviewAdjustmentTelemetry adjustment_telemetry;
    if (!apply_color_adjustments(&edited_image, request.state, &request, &adjustment_telemetry) &&
        render_request_superseded(request)) {
      result.cancelled = true;
      result.total_elapsed_ms = total_timer.elapsed();
      result.edit_elapsed_ms = edit_timer.elapsed();
      return result;
    }
    result.backend =
        adjustment_telemetry.backend.isEmpty() ? QStringLiteral("unknown") : adjustment_telemetry.backend;
    result.fallback_reason = adjustment_telemetry.fallback_reason;
    result.linear_cache_lookup = adjustment_telemetry.linear_cache_lookup;
    result.linear_cache_hit = adjustment_telemetry.linear_cache_hit;
    result.linear_cache_entries = adjustment_telemetry.linear_cache.entries;
    result.linear_cache_bytes = adjustment_telemetry.linear_cache.bytes;
  }
  apply_preview_display_transform(&edited_image, request.state);
  result.edit_elapsed_ms = edit_timer.elapsed();
  result.output_width = edited_image.width();
  result.output_height = edited_image.height();
  if (render_request_superseded(request)) {
    result.cancelled = true;
    result.total_elapsed_ms = total_timer.elapsed();
    return result;
  }

  QElapsedTimer encode_timer;
  encode_timer.start();
  if (!request.full_resolution) {
    result.preview_image = edited_image;
  }
  if (request.use_in_memory_preview) {
    result.display_preview_source =
        QStringLiteral("image://editor-preview/%1").arg(QString::fromLatin1(kEditedPreviewImageId));
  } else {
    const auto output_path = cache_path_for_selection(
        request.file_path,
        request.state,
        true,
        request.full_resolution,
        request.bypass_crop,
        request.max_edge);
    QImageWriter writer(output_path, "jpg");
    writer.setQuality(request.jpeg_quality);
    writer.setOptimizedWrite(false);
    if (render_request_superseded(request)) {
      result.cancelled = true;
      result.total_elapsed_ms = total_timer.elapsed();
      result.encode_elapsed_ms = encode_timer.elapsed();
      return result;
    }
    if (!writer.write(edited_image)) {
      result.status = QStringLiteral("Unable to write preview cache.");
      result.total_elapsed_ms = total_timer.elapsed();
      result.encode_elapsed_ms = encode_timer.elapsed();
      observability::logWarning(
          photoEditorPreviewLog(),
          QStringLiteral("preview.render.failed"),
          {
              {QStringLiteral("stage"), request.full_resolution ? QStringLiteral("full") : QStringLiteral("interactive")},
              {QStringLiteral("generation"), request.generation},
              {QStringLiteral("render_reason"), request.render_reason},
              {QStringLiteral("interaction_mode"), request.interaction_mode},
              {QStringLiteral("file_path"), request.file_path},
              {QStringLiteral("elapsed_ms"), result.total_elapsed_ms},
              {QStringLiteral("status"), result.status},
          });
      return result;
    }

    result.preview_source = versioned_file_url(output_path);
    result.display_preview_source = result.preview_source;
    result.updated_persistent_sources = true;
  }
  result.histogram_image =
      edited_image.width() > kHistogramPreviewEdge || edited_image.height() > kHistogramPreviewEdge
          ? edited_image.scaled(
                kHistogramPreviewEdge,
                kHistogramPreviewEdge,
                Qt::KeepAspectRatio,
                Qt::FastTransformation)
          : edited_image;
  result.preview_overlay_rect = request.preview_overlay_rect;
  if (request.use_in_memory_preview && !request.compare_mode &&
      request.preview_overlay_rect.isValid() && request.preview_overlay_max_edge > 0) {
    result.preview_overlay_image = render_preview_overlay_patch(request, true);
    if (!result.preview_overlay_image.isNull()) {
      result.display_preview_overlay_source =
          QStringLiteral("image://editor-preview/%1")
              .arg(QString::fromLatin1(kEditedPreviewOverlayImageId));
    }
  }

  if (request.compare_mode) {
    const auto external_reference = !request.reference_file_path.isEmpty();
    const auto compare_adjustment_groups =
        external_reference ? QStringList{} : normalized_preset_adjustment_groups(request.compare_adjustment_groups);
    auto reference_state = request.state;
    if (!compare_adjustment_groups.isEmpty()) {
      reference_state =
          merge_edit_recipe_state_by_groups(request.state, EditRecipeState{}, compare_adjustment_groups);
    }
    QImage reference_image;
    if (external_reference) {
      if (!decode_source_image(request.reference_file_path, &reference_image, request.max_edge, request.state)) {
        result.status = QStringLiteral("Reference image unavailable.");
        result.total_elapsed_ms = total_timer.elapsed();
        result.decode_elapsed_ms = decode_timer.elapsed();
        return result;
      }
    } else {
      reference_image = request.bypass_crop ? source_image : apply_crop(source_image, reference_state);
      if (!compare_adjustment_groups.isEmpty() && has_visible_adjustments(reference_state)) {
        auto reference_request = request;
        reference_request.state = reference_state;
        reference_request.compare_mode = false;
        if (!apply_color_adjustments(&reference_image, reference_state, &reference_request) &&
            render_request_superseded(request)) {
          result.cancelled = true;
          return result;
        }
      }
    }
    if (render_request_superseded(request)) {
      result.cancelled = true;
      return result;
    }
    apply_preview_display_transform(&reference_image, external_reference ? request.state : reference_state);
    if (!request.full_resolution) {
      result.reference_preview_image = reference_image;
    }
    if (request.use_in_memory_preview) {
      result.display_reference_preview_source =
          QStringLiteral("image://editor-preview/%1").arg(QString::fromLatin1(kReferencePreviewImageId));
    } else {
      const auto reference_output_path = cache_path_for_selection(
          external_reference ? request.reference_file_path + QStringLiteral("|external-reference")
                             : request.file_path + QStringLiteral("|compare-selected"),
          external_reference ? request.state : reference_state,
          false,
          request.full_resolution,
          external_reference ? true : request.bypass_crop,
          request.max_edge);
      if (!QFileInfo::exists(reference_output_path)) {
        QImageWriter reference_writer(reference_output_path, "jpg");
        reference_writer.setQuality(request.jpeg_quality);
        reference_writer.setOptimizedWrite(false);
        if (render_request_superseded(request)) {
          result.cancelled = true;
          result.total_elapsed_ms = total_timer.elapsed();
          result.encode_elapsed_ms = encode_timer.elapsed();
          return result;
        }
        if (!reference_writer.write(reference_image)) {
          result.status = QStringLiteral("Unable to write reference preview cache.");
          result.total_elapsed_ms = total_timer.elapsed();
          result.encode_elapsed_ms = encode_timer.elapsed();
          observability::logWarning(
              photoEditorPreviewLog(),
              QStringLiteral("preview.render.failed"),
              {
                  {QStringLiteral("stage"), request.full_resolution ? QStringLiteral("full") : QStringLiteral("interactive")},
                  {QStringLiteral("generation"), request.generation},
                  {QStringLiteral("render_reason"), request.render_reason},
                  {QStringLiteral("interaction_mode"), request.interaction_mode},
                  {QStringLiteral("file_path"), request.file_path},
                  {QStringLiteral("elapsed_ms"), result.total_elapsed_ms},
                  {QStringLiteral("status"), result.status},
              });
          return result;
        }
      }
      result.reference_preview_source = versioned_file_url(reference_output_path);
      result.display_reference_preview_source = result.reference_preview_source;
      result.updated_persistent_sources = true;
    }
  }
  result.encode_elapsed_ms = encode_timer.elapsed();

  const auto linear_cache = linear_image_cache_snapshot();
  result.linear_cache_entries = std::max(result.linear_cache_entries, linear_cache.entries);
  result.linear_cache_bytes = linear_cache.bytes;
  result.success = true;
  if (has_visible_adjustments(request.state)) {
    result.status = request.full_resolution ? QStringLiteral("Full-resolution preview ready.")
                                            : QStringLiteral("Interactive preview ready.");
  } else {
    result.status = request.full_resolution ? QStringLiteral("Full-resolution base preview ready.")
                                            : QStringLiteral("Base preview ready.");
  }
  result.total_elapsed_ms = total_timer.elapsed();
  observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("preview.render.completed"),
      {
          {QStringLiteral("stage"), request.full_resolution ? QStringLiteral("full") : QStringLiteral("interactive")},
          {QStringLiteral("generation"), request.generation},
          {QStringLiteral("render_reason"), request.render_reason},
          {QStringLiteral("interaction_mode"), request.interaction_mode},
          {QStringLiteral("file_path"), request.file_path},
          {QStringLiteral("source_width"), result.source_width},
          {QStringLiteral("source_height"), result.source_height},
          {QStringLiteral("output_width"), result.output_width},
          {QStringLiteral("output_height"), result.output_height},
          {QStringLiteral("has_adjustments"), result.had_adjustments},
          {QStringLiteral("compare_mode"), request.compare_mode},
          {QStringLiteral("in_memory_preview"), request.use_in_memory_preview},
          {QStringLiteral("draft_interactive"), request.draft_interactive},
          {QStringLiteral("max_edge"), request.max_edge},
          {QStringLiteral("backend"), result.backend},
          {QStringLiteral("fallback_reason"), result.fallback_reason},
          {QStringLiteral("decode_backend"), result.decode_backend},
          {QStringLiteral("decoded_cache_lookup"), result.decoded_cache_lookup},
          {QStringLiteral("decoded_cache_hit"), result.decoded_cache_hit},
          {QStringLiteral("decoded_cache_entries"), result.decoded_cache_entries},
          {QStringLiteral("decoded_cache_bytes"), result.decoded_cache_bytes},
          {QStringLiteral("linear_cache_lookup"), result.linear_cache_lookup},
          {QStringLiteral("linear_cache_hit"), result.linear_cache_hit},
          {QStringLiteral("linear_cache_entries"), result.linear_cache_entries},
          {QStringLiteral("linear_cache_bytes"), result.linear_cache_bytes},
          {QStringLiteral("queue_wait_ms"), result.queue_wait_ms},
          {QStringLiteral("decode_ms"), result.decode_elapsed_ms},
          {QStringLiteral("edit_ms"), result.edit_elapsed_ms},
          {QStringLiteral("encode_ms"), result.encode_elapsed_ms},
          {QStringLiteral("elapsed_ms"), result.total_elapsed_ms},
      });
  return result;
}

}  // namespace

EditorPreviewController::EditorPreviewController(QObject* parent) : QObject(parent) {
  brush_preview_compositor_ = BrushPreviewCompositor::create();
  display_preview_texture_ = EditorPreviewTexture::create();
  loadPresets();
  loadCameraDefaults();
  loadCustomCropPresets();

  interactive_render_timer_.setSingleShot(true);
  interactive_render_timer_.setInterval(interactive_render_debounce_ms_for_state(currentState()));
  connect(&interactive_render_timer_, &QTimer::timeout, this, [this]() { renderPreviewStage(false); });

  full_render_timer_.setSingleShot(true);
  full_render_timer_.setInterval(full_render_debounce_ms_for_state(currentState()));
  connect(&full_render_timer_, &QTimer::timeout, this, [this]() { renderPreviewStage(true); });

  smart_select_preview_timer_.setSingleShot(true);
  smart_select_preview_timer_.setInterval(kSmartSelectPreviewDebounceMs);
  connect(&smart_select_preview_timer_, &QTimer::timeout, this, [this]() {
    startSmartSelectPreviewRequest();
  });

  connect(&interactive_render_watcher_, &QFutureWatcher<RenderResult>::finished, this, [this]() {
    handleRenderFinished(false);
  });
  connect(&full_render_watcher_, &QFutureWatcher<RenderResult>::finished, this, [this]() {
    handleRenderFinished(true);
  });
  connect(&histogram_watcher_, &QFutureWatcher<HistogramComputationResult>::finished, this, [this]() {
    handleHistogramFinished();
  });
  connect(
      &smart_select_preview_watcher_,
      &QFutureWatcher<SmartSelectPreviewResult>::finished,
      this,
      [this]() { handleSmartSelectPreviewFinished(); });

  persist_timer_.setSingleShot(true);
  persist_timer_.setInterval(350);
  connect(&persist_timer_, &QTimer::timeout, this, &EditorPreviewController::flushPersistedState);

  connect(&persist_process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
    setStatus(QStringLiteral("Edit recipe save is unavailable."));
    refreshDebugInfo();
  });
}

QString EditorPreviewController::previewSource() const { return preview_source_; }
QString EditorPreviewController::displayPreviewSource() const {
  return display_preview_source_.isEmpty() ? preview_source_ : display_preview_source_;
}
QString EditorPreviewController::displayPreviewOverlaySource() const {
  return display_preview_overlay_source_;
}
bool EditorPreviewController::displayPreviewNativeAvailable() const {
  return display_preview_texture_ != nullptr &&
         display_preview_texture_->hasNativeTexture() &&
         display_preview_texture_->nativeTextureHandle() != 0 &&
         display_preview_texture_->nativeTextureSize().isValid();
}
quint64 EditorPreviewController::displayPreviewNativeHandle() const {
  return display_preview_texture_ != nullptr ? display_preview_texture_->nativeTextureHandle() : 0;
}
QSize EditorPreviewController::displayPreviewNativeSize() const {
  return display_preview_texture_ != nullptr ? display_preview_texture_->nativeTextureSize() : QSize{};
}
int EditorPreviewController::displayPreviewNativeRevision() const {
  return display_preview_native_revision_;
}
QString EditorPreviewController::referencePreviewSource() const { return reference_preview_source_; }
QString EditorPreviewController::displayReferencePreviewSource() const {
  return display_reference_preview_source_.isEmpty() ? reference_preview_source_
                                                     : display_reference_preview_source_;
}
QString EditorPreviewController::displayReferencePreviewOverlaySource() const {
  return display_reference_preview_overlay_source_;
}
QString EditorPreviewController::referenceImagePath() const { return reference_image_path_; }
QString EditorPreviewController::referenceImageName() const { return reference_image_name_; }
bool EditorPreviewController::hasReferenceImage() const { return !reference_image_path_.isEmpty(); }
QRectF EditorPreviewController::displayPreviewOverlayRect() const {
  return display_preview_overlay_rect_;
}
QVariantList EditorPreviewController::histogramRed() const { return histogram_red_; }
QVariantList EditorPreviewController::histogramGreen() const { return histogram_green_; }
QVariantList EditorPreviewController::histogramBlue() const { return histogram_blue_; }
QVariantList EditorPreviewController::histogramLuminance() const { return histogram_luminance_; }
QVariantList EditorPreviewController::scopeWaveformRed() const { return scope_waveform_red_; }
QVariantList EditorPreviewController::scopeWaveformGreen() const { return scope_waveform_green_; }
QVariantList EditorPreviewController::scopeWaveformBlue() const { return scope_waveform_blue_; }
QVariantList EditorPreviewController::scopeWaveformLuminance() const { return scope_waveform_luminance_; }
QVariantList EditorPreviewController::scopeDensityRed() const { return scope_density_red_; }
QVariantList EditorPreviewController::scopeDensityGreen() const { return scope_density_green_; }
QVariantList EditorPreviewController::scopeDensityBlue() const { return scope_density_blue_; }
QVariantList EditorPreviewController::scopeDensityLuminance() const { return scope_density_luminance_; }
QVariantList EditorPreviewController::scopeVectorscopePoints() const { return scope_vectorscope_points_; }
int EditorPreviewController::histogramShadowClipCount() const { return histogram_shadow_clip_count_; }
int EditorPreviewController::histogramHighlightClipCount() const { return histogram_highlight_clip_count_; }
int EditorPreviewController::histogramSampleCount() const { return histogram_sample_count_; }
int EditorPreviewController::rawClipSampleCount() const { return raw_clip_sample_count_; }
int EditorPreviewController::rawShadowClipCount() const { return raw_shadow_clip_count_; }
int EditorPreviewController::rawHighlightClipCount() const { return raw_highlight_clip_count_; }
QVariantMap EditorPreviewController::pixelInspector() const { return pixel_inspector_; }
bool EditorPreviewController::histogramUpdateActive() const { return histogram_update_active_; }
int EditorPreviewController::previewTargetEdge() const { return clamp_preview_target_edge(preview_target_edge_); }
QString EditorPreviewController::withDisplayRevision(const QString& source) const {
  return with_preview_revision(source, preview_revision_);
}
void EditorPreviewController::setImageProvider(EditorPreviewImageSink* image_provider) {
  image_provider_ = image_provider;
  if (image_provider_ != nullptr && !active_brush_live_preview_image_.isNull()) {
    image_provider_->setImage(
        QString::fromLatin1(kActiveBrushLivePreviewImageId),
        active_brush_live_preview_image_);
  }
}
QString EditorPreviewController::debugInfo() const { return debug_info_; }
QString EditorPreviewController::status() const { return status_; }
QString EditorPreviewController::assetId() const { return asset_id_; }
QString EditorPreviewController::cameraLabel() const { return camera_label_; }
QString EditorPreviewController::captureDate() const { return capture_date_; }
QString EditorPreviewController::lensModel() const { return lens_model_; }
bool EditorPreviewController::hasAsset() const { return !file_path_.isEmpty() || !fallback_thumbnail_source_.isEmpty(); }
bool EditorPreviewController::subjectMaskAvailable() const { return vision_segmentation_available(); }
QString EditorPreviewController::aiModelStatus() const { return ai_model_runtime_status().summary; }
bool EditorPreviewController::aiSmartSelectionAvailable() const {
  return ai_model_runtime_status().smart_selection_available;
}
bool EditorPreviewController::aiInpaintAvailable() const {
  return ai_model_runtime_status().inpaint_available;
}
QString EditorPreviewController::inpaintPrompt() const { return inpaint_prompt_; }
int EditorPreviewController::inpaintSeed() const { return inpaint_seed_; }
double EditorPreviewController::inpaintBlendAmount() const { return inpaint_blend_amount_; }
QString EditorPreviewController::inpaintQualityMode() const { return inpaint_quality_mode_; }
bool EditorPreviewController::retouchBeforeAfterEnabled() const {
  return retouch_before_after_enabled_;
}
bool EditorPreviewController::inpaintPatchDiagnosticsEnabled() const {
  return inpaint_patch_diagnostics_enabled_;
}
QRectF EditorPreviewController::activeInpaintPatchBounds() const {
  const auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return {};
  }

  const auto patch_x = mask.value(QStringLiteral("inpaintPatchX")).toInt();
  const auto patch_y = mask.value(QStringLiteral("inpaintPatchY")).toInt();
  const auto patch_width = mask.value(QStringLiteral("inpaintPatchWidth")).toInt();
  const auto patch_height = mask.value(QStringLiteral("inpaintPatchHeight")).toInt();
  if (patch_width <= 0 || patch_height <= 0) {
    return {};
  }

  auto source_width = mask.value(QStringLiteral("inpaintSourceMaskWidth")).toInt();
  auto source_height = mask.value(QStringLiteral("inpaintSourceMaskHeight")).toInt();
  if (source_width <= 0 || source_height <= 0) {
    const auto source_size = sourceImageSize();
    source_width = source_size.width();
    source_height = source_size.height();
  }
  if (source_width <= 0 || source_height <= 0) {
    return {};
  }

  return QRectF(
             static_cast<double>(patch_x) / static_cast<double>(source_width),
             static_cast<double>(patch_y) / static_cast<double>(source_height),
             static_cast<double>(patch_width) / static_cast<double>(source_width),
             static_cast<double>(patch_height) / static_cast<double>(source_height))
      .intersected(QRectF(0.0, 0.0, 1.0, 1.0));
}
bool EditorPreviewController::processing() const { return processing_; }
bool EditorPreviewController::canUndo() const {
  return !asset_id_.isEmpty() && !asset_undo_states_.value(asset_id_).isEmpty();
}
bool EditorPreviewController::canRedo() const {
  return !asset_id_.isEmpty() && !asset_redo_states_.value(asset_id_).isEmpty();
}
bool EditorPreviewController::hasCopiedSettings() const { return has_copied_settings_; }
QStringList EditorPreviewController::presetNames() const {
  auto names = built_in_presets_.keys();
  for (const auto& name : presets_.keys()) {
    if (!names.contains(name)) {
      names.push_back(name);
    }
  }
  names.sort(Qt::CaseInsensitive);
  return names;
}
QStringList EditorPreviewController::presetPackNames() const {
  QStringList packs;
  for (const auto& pack : built_in_preset_packs_) {
    if (!packs.contains(pack)) {
      packs.push_back(pack);
    }
  }
  for (const auto& pack : user_preset_packs_) {
    const auto normalized_pack = pack.simplified().isEmpty() ? QStringLiteral("User Presets") : pack.simplified();
    if (!packs.contains(normalized_pack)) {
      packs.push_back(normalized_pack);
    }
  }
  if (!presets_.isEmpty() && packs.indexOf(QStringLiteral("User Presets")) < 0) {
    packs.push_back(QStringLiteral("User Presets"));
  }
  return packs;
}
QStringList EditorPreviewController::presetCategoryNames() const {
  QStringList categories;
  for (const auto& category : built_in_preset_categories_) {
    const auto normalized = normalized_preset_category(category, QStringLiteral("Built In"));
    if (!categories.contains(normalized)) {
      categories.push_back(normalized);
    }
  }
  for (const auto& category : user_preset_categories_) {
    const auto normalized = normalized_preset_category(category, QStringLiteral("User"));
    if (!categories.contains(normalized)) {
      categories.push_back(normalized);
    }
  }
  categories.sort(Qt::CaseInsensitive);
  return categories;
}
QStringList EditorPreviewController::favoritePresetNames() const {
  const auto names = presetNames();
  QStringList favorites;
  for (const auto& name : favorite_presets_) {
    if (names.contains(name) && !favorites.contains(name)) {
      favorites.push_back(name);
    }
  }
  favorites.sort(Qt::CaseInsensitive);
  return favorites;
}
double EditorPreviewController::presetIntensity() const { return preset_intensity_; }
bool EditorPreviewController::hasCameraDefault() const {
  const auto key = normalizedCameraDefaultKey(camera_label_);
  return !key.isEmpty() && camera_default_states_.contains(key);
}
bool EditorPreviewController::compareMode() const { return compare_mode_; }
QStringList EditorPreviewController::compareAdjustmentGroups() const { return compare_adjustment_groups_; }
bool EditorPreviewController::cropEditing() const { return crop_editing_; }
QString EditorPreviewController::inputColorProfile() const { return input_color_profile_; }
QString EditorPreviewController::outputColorSpace() const { return output_color_space_; }
QString EditorPreviewController::monitorColorProfile() const { return monitor_color_profile_; }
bool EditorPreviewController::softProofEnabled() const { return soft_proof_enabled_; }
QString EditorPreviewController::softProofProfile() const { return soft_proof_profile_; }
bool EditorPreviewController::rawControlsAvailable() const { return raw_image_supported(file_path_); }
double EditorPreviewController::rawExposure() const { return raw_exposure_; }
double EditorPreviewController::rawBoost() const { return raw_boost_; }
double EditorPreviewController::rawShadowBoost() const { return raw_shadow_boost_; }
double EditorPreviewController::rawDetail() const { return raw_detail_; }
double EditorPreviewController::rawChromaNoiseReduction() const { return raw_chroma_noise_reduction_; }
double EditorPreviewController::rawMoireReduction() const { return raw_moire_reduction_; }
double EditorPreviewController::rawHotDeadPixelCleanup() const { return raw_hot_dead_pixel_cleanup_; }
double EditorPreviewController::rawFalseColorSuppression() const { return raw_false_color_suppression_; }
bool EditorPreviewController::rawHighlightRecovery() const { return raw_highlight_recovery_; }
QString EditorPreviewController::highlightReconstructionMode() const { return highlight_reconstruction_mode_; }
QString EditorPreviewController::rawDemosaicQuality() const { return raw_demosaic_quality_; }
bool EditorPreviewController::rawGamutMapping() const { return raw_gamut_mapping_; }
bool EditorPreviewController::rawLensCorrection() const { return raw_lens_correction_; }
bool EditorPreviewController::rawWhiteBalanceOverride() const { return raw_white_balance_override_; }
QString EditorPreviewController::rawCameraMatchingLook() const { return raw_camera_matching_look_; }
double EditorPreviewController::rawWhiteBalanceTemperature() const { return raw_white_balance_temperature_; }
double EditorPreviewController::rawWhiteBalanceTint() const { return raw_white_balance_tint_; }
double EditorPreviewController::rawAsShotWhiteBalanceTemperature() const {
  return raw_as_shot_white_balance_temperature_;
}
double EditorPreviewController::rawAsShotWhiteBalanceTint() const {
  return raw_as_shot_white_balance_tint_;
}
double EditorPreviewController::rawBlackLevel() const { return raw_black_level_; }
double EditorPreviewController::rawWhiteLevel() const { return raw_white_level_; }
double EditorPreviewController::rawCalibrationHue() const { return raw_calibration_hue_; }
double EditorPreviewController::rawCalibrationSaturation() const { return raw_calibration_saturation_; }
double EditorPreviewController::rawRedPrimaryHue() const { return raw_red_primary_hue_; }
double EditorPreviewController::rawRedPrimarySaturation() const { return raw_red_primary_saturation_; }
double EditorPreviewController::rawGreenPrimaryHue() const { return raw_green_primary_hue_; }
double EditorPreviewController::rawGreenPrimarySaturation() const { return raw_green_primary_saturation_; }
double EditorPreviewController::rawBluePrimaryHue() const { return raw_blue_primary_hue_; }
double EditorPreviewController::rawBluePrimarySaturation() const { return raw_blue_primary_saturation_; }
double EditorPreviewController::exposure() const { return exposure_; }
double EditorPreviewController::contrast() const { return contrast_; }
double EditorPreviewController::highlights() const { return highlights_; }
double EditorPreviewController::shadows() const { return shadows_; }
double EditorPreviewController::whites() const { return whites_; }
double EditorPreviewController::blacks() const { return blacks_; }
double EditorPreviewController::brightness() const { return brightness_; }
double EditorPreviewController::hdrLocalToneMapping() const { return hdr_local_tone_mapping_; }
double EditorPreviewController::filmicShoulder() const { return filmic_shoulder_; }
double EditorPreviewController::filmicToe() const { return filmic_toe_; }
double EditorPreviewController::adaptiveContrast() const { return adaptive_contrast_; }
double EditorPreviewController::levelsBlackPoint() const { return levels_black_point_; }
double EditorPreviewController::levelsGamma() const { return levels_gamma_; }
double EditorPreviewController::levelsWhitePoint() const { return levels_white_point_; }
double EditorPreviewController::zoneDeepShadows() const { return zone_deep_shadows_; }
double EditorPreviewController::zoneShadows() const { return zone_shadows_; }
double EditorPreviewController::zoneMidtones() const { return zone_midtones_; }
double EditorPreviewController::zoneLights() const { return zone_lights_; }
double EditorPreviewController::zoneHighlights() const { return zone_highlights_; }
double EditorPreviewController::temperature() const { return temperature_; }
double EditorPreviewController::tint() const { return tint_; }
double EditorPreviewController::texture() const { return texture_; }
double EditorPreviewController::clarity() const { return clarity_; }
double EditorPreviewController::dehaze() const { return dehaze_; }
double EditorPreviewController::sharpening() const { return sharpening_; }
double EditorPreviewController::sharpenMasking() const { return sharpen_masking_; }
double EditorPreviewController::luminanceNr() const { return luminance_nr_; }
double EditorPreviewController::colorNr() const { return color_nr_; }
double EditorPreviewController::lensDistortion() const { return lens_distortion_; }
double EditorPreviewController::lensVignette() const { return lens_vignette_; }
double EditorPreviewController::chromaticAberration() const { return chromatic_aberration_; }
double EditorPreviewController::perspectiveVertical() const { return perspective_vertical_; }
double EditorPreviewController::perspectiveHorizontal() const { return perspective_horizontal_; }
double EditorPreviewController::transformRotation() const { return transform_rotation_; }
double EditorPreviewController::transformScale() const { return transform_scale_; }
double EditorPreviewController::transformSkewX() const { return transform_skew_x_; }
double EditorPreviewController::transformSkewY() const { return transform_skew_y_; }
double EditorPreviewController::liquifyCenterX() const { return liquify_center_x_; }
double EditorPreviewController::liquifyCenterY() const { return liquify_center_y_; }
double EditorPreviewController::liquifyRadius() const { return liquify_radius_; }
double EditorPreviewController::liquifyStrength() const { return liquify_strength_; }
bool EditorPreviewController::contentAwareCropFill() const { return content_aware_crop_fill_; }
double EditorPreviewController::vibrance() const { return vibrance_; }
double EditorPreviewController::saturation() const { return saturation_; }
double EditorPreviewController::curveShadows() const { return curve_shadows_; }
double EditorPreviewController::curveDarks() const { return curve_darks_; }
double EditorPreviewController::curveLights() const { return curve_lights_; }
double EditorPreviewController::curveHighlights() const { return curve_highlights_; }
QString EditorPreviewController::curveChannel() const { return curve_channel_; }
QString EditorPreviewController::activeCurveMode() const {
  const auto normalized_channel = curve_channel_.trimmed().isEmpty() ? QStringLiteral("RGB") : curve_channel_;
  if (normalized_channel == QStringLiteral("RGB") || normalized_channel == QStringLiteral("Master")) {
    if (!master_curve_points_.isEmpty()) {
      return QStringLiteral("Point");
    }
    if (!qFuzzyIsNull(curve_shadows_) || !qFuzzyIsNull(curve_darks_) ||
        !qFuzzyIsNull(curve_lights_) || !qFuzzyIsNull(curve_highlights_)) {
      return QStringLiteral("Parametric");
    }
    return QStringLiteral("Neutral");
  }
  if (normalized_channel == QStringLiteral("Luma")) {
    return luma_curve_points_.isEmpty() ? QStringLiteral("Neutral") : QStringLiteral("Point");
  }
  if (normalized_channel == QStringLiteral("Red")) {
    return red_curve_points_.isEmpty() ? QStringLiteral("Neutral") : QStringLiteral("Point");
  }
  if (normalized_channel == QStringLiteral("Green")) {
    return green_curve_points_.isEmpty() ? QStringLiteral("Neutral") : QStringLiteral("Point");
  }
  return blue_curve_points_.isEmpty() ? QStringLiteral("Neutral") : QStringLiteral("Point");
}
QVariantList EditorPreviewController::activeCurvePoints() const {
  const auto normalized_channel = curve_channel_.trimmed().isEmpty() ? QStringLiteral("RGB") : curve_channel_;
  if (normalized_channel == QStringLiteral("RGB") || normalized_channel == QStringLiteral("Master")) {
    if (!master_curve_points_.isEmpty()) {
      return master_curve_points_;
    }
    if (!qFuzzyIsNull(curve_shadows_) || !qFuzzyIsNull(curve_darks_) ||
        !qFuzzyIsNull(curve_lights_) || !qFuzzyIsNull(curve_highlights_)) {
      return parametric_curve_points(curve_shadows_, curve_darks_, curve_lights_, curve_highlights_);
    }
    return default_curve_points();
  }
  if (normalized_channel == QStringLiteral("Luma")) {
    return luma_curve_points_.isEmpty() ? default_curve_points() : luma_curve_points_;
  }
  if (normalized_channel == QStringLiteral("Red")) {
    return red_curve_points_.isEmpty() ? default_curve_points() : red_curve_points_;
  }
  if (normalized_channel == QStringLiteral("Green")) {
    return green_curve_points_.isEmpty() ? default_curve_points() : green_curve_points_;
  }
  return blue_curve_points_.isEmpty() ? default_curve_points() : blue_curve_points_;
}
QStringList EditorPreviewController::curvePresetNames() const { return curve_preset_names(); }
QString EditorPreviewController::colorMixerModel() const { return color_mixer_model_; }
QString EditorPreviewController::hslChannel() const { return hsl_channel_; }
double EditorPreviewController::hueAdjustment() const { return hue_adjustment_; }
double EditorPreviewController::saturationAdjustment() const { return saturation_adjustment_; }
double EditorPreviewController::luminanceAdjustment() const { return luminance_adjustment_; }
double EditorPreviewController::selectiveCyan() const {
  const auto index = mixer_channel_index(hsl_channel_);
  return index >= 0 ? mixer_channels_[static_cast<std::size_t>(index)].selective_cyan : 0.0;
}
double EditorPreviewController::selectiveMagenta() const {
  const auto index = mixer_channel_index(hsl_channel_);
  return index >= 0 ? mixer_channels_[static_cast<std::size_t>(index)].selective_magenta : 0.0;
}
double EditorPreviewController::selectiveYellow() const {
  const auto index = mixer_channel_index(hsl_channel_);
  return index >= 0 ? mixer_channels_[static_cast<std::size_t>(index)].selective_yellow : 0.0;
}
double EditorPreviewController::selectiveBlack() const {
  const auto index = mixer_channel_index(hsl_channel_);
  return index >= 0 ? mixer_channels_[static_cast<std::size_t>(index)].selective_black : 0.0;
}
double EditorPreviewController::channelMixerRedFromRed() const { return rgb_channel_mixer_.red_from_red; }
double EditorPreviewController::channelMixerRedFromGreen() const { return rgb_channel_mixer_.red_from_green; }
double EditorPreviewController::channelMixerRedFromBlue() const { return rgb_channel_mixer_.red_from_blue; }
double EditorPreviewController::channelMixerGreenFromRed() const { return rgb_channel_mixer_.green_from_red; }
double EditorPreviewController::channelMixerGreenFromGreen() const { return rgb_channel_mixer_.green_from_green; }
double EditorPreviewController::channelMixerGreenFromBlue() const { return rgb_channel_mixer_.green_from_blue; }
double EditorPreviewController::channelMixerBlueFromRed() const { return rgb_channel_mixer_.blue_from_red; }
double EditorPreviewController::channelMixerBlueFromGreen() const { return rgb_channel_mixer_.blue_from_green; }
double EditorPreviewController::channelMixerBlueFromBlue() const { return rgb_channel_mixer_.blue_from_blue; }
bool EditorPreviewController::blackAndWhiteEnabled() const { return black_and_white_enabled_; }
double EditorPreviewController::blackAndWhiteReds() const { return black_and_white_mixer_[0]; }
double EditorPreviewController::blackAndWhiteOranges() const { return black_and_white_mixer_[1]; }
double EditorPreviewController::blackAndWhiteYellows() const { return black_and_white_mixer_[2]; }
double EditorPreviewController::blackAndWhiteGreens() const { return black_and_white_mixer_[3]; }
double EditorPreviewController::blackAndWhiteAquas() const { return black_and_white_mixer_[4]; }
double EditorPreviewController::blackAndWhiteBlues() const { return black_and_white_mixer_[5]; }
double EditorPreviewController::blackAndWhitePurples() const { return black_and_white_mixer_[6]; }
double EditorPreviewController::blackAndWhiteMagentas() const { return black_and_white_mixer_[7]; }
bool EditorPreviewController::colorReplacementEnabled() const { return color_replacement_enabled_; }
double EditorPreviewController::colorReplacementSourceHue() const { return color_replacement_source_hue_; }
double EditorPreviewController::colorReplacementTargetHue() const { return color_replacement_target_hue_; }
double EditorPreviewController::colorReplacementRange() const { return color_replacement_range_; }
double EditorPreviewController::colorReplacementStrength() const { return color_replacement_strength_; }
bool EditorPreviewController::skinToneProtectionEnabled() const { return skin_tone_protection_enabled_; }
double EditorPreviewController::skinToneLineHue() const { return skin_tone_line_hue_; }
double EditorPreviewController::skinToneLineAngle() const { return skin_tone_line_hue_ * 360.0; }
double EditorPreviewController::skinToneProtectionRange() const { return skin_tone_protection_range_; }
double EditorPreviewController::skinToneProtectionStrength() const { return skin_tone_protection_strength_; }
bool EditorPreviewController::colorHarmonyEnabled() const { return color_harmony_enabled_; }
QString EditorPreviewController::colorHarmonyMode() const { return color_harmony_mode_; }
double EditorPreviewController::colorHarmonyBaseHue() const { return color_harmony_base_hue_; }
double EditorPreviewController::colorHarmonyStrength() const { return color_harmony_strength_; }
double EditorPreviewController::gradingShadowsHue() const { return grading_shadows_hue_; }
double EditorPreviewController::gradingShadowsSaturation() const { return grading_shadows_saturation_; }
double EditorPreviewController::gradingShadowsLuminance() const { return grading_shadows_luminance_; }
double EditorPreviewController::gradingMidtonesHue() const { return grading_midtones_hue_; }
double EditorPreviewController::gradingMidtonesSaturation() const { return grading_midtones_saturation_; }
double EditorPreviewController::gradingMidtonesLuminance() const { return grading_midtones_luminance_; }
double EditorPreviewController::gradingHighlightsHue() const { return grading_highlights_hue_; }
double EditorPreviewController::gradingHighlightsSaturation() const { return grading_highlights_saturation_; }
double EditorPreviewController::gradingHighlightsLuminance() const { return grading_highlights_luminance_; }
double EditorPreviewController::cropRectX() const {
  return normalized_crop_rect_for_state(sourceImageSize(), currentState()).x();
}
double EditorPreviewController::cropRectY() const {
  return normalized_crop_rect_for_state(sourceImageSize(), currentState()).y();
}
double EditorPreviewController::cropRectWidth() const {
  return normalized_crop_rect_for_state(sourceImageSize(), currentState()).width();
}
double EditorPreviewController::cropRectHeight() const {
  return normalized_crop_rect_for_state(sourceImageSize(), currentState()).height();
}
double EditorPreviewController::cropAspectRatio() const { return crop_aspect_ratio(crop_preset_); }
double EditorPreviewController::cropZoom() const { return crop_zoom_; }
QString EditorPreviewController::cropPreset() const { return crop_preset_; }
QString EditorPreviewController::uprightMode() const { return upright_mode_; }
double EditorPreviewController::uprightVerticalGuideAngle() const { return upright_vertical_guide_angle_; }
double EditorPreviewController::uprightHorizontalGuideAngle() const { return upright_horizontal_guide_angle_; }
QVariantList EditorPreviewController::maskSets() const {
  if (mask_group_cache_dirty_) {
    cached_mask_sets_ = grouped_mask_sets(mask_stack_);
    cached_retouch_stack_ = grouped_retouch_stack(mask_stack_);
    if (!cached_mask_sets_.isEmpty() && hasAsset()) {
      const auto state = currentState();
      QImage source_image;
      if (decodeSourceImage(&source_image, 96)) {
        const auto cropped_image = applyCrop(source_image);
        const auto exposure_gain = std::pow(2.0, state.exposure);
        const auto white_balance = preview_white_balance_multipliers(state.temperature, state.tint);
        const auto thumbnail_source_for_masks =
            [&](const QVariantList& masks, const QVariantMap& color_reference_mask) {
              if (masks.isEmpty() || cropped_image.isNull()) {
                return QString();
              }
              std::vector<photo_editor::pipeline::LocalMask> composite_masks;
              composite_masks.reserve(static_cast<std::size_t>(masks.size()));
              for (const auto& mask_value : masks) {
                composite_masks.push_back(pipeline_local_mask(mask_value.toMap()));
              }
              return composite_mask_preview_source(
                  cropped_image,
                  composite_masks,
                  color_reference_mask,
                  QStringLiteral("white-on-black"),
                  exposure_gain,
                  white_balance,
                  0.95);
            };

        for (auto set_index = 0; set_index < cached_mask_sets_.size(); ++set_index) {
          auto set = cached_mask_sets_.at(set_index).toMap();
          auto sources = set.value(QStringLiteral("sources")).toList();
          QVariantList thumbnail_masks;
          QVariantMap color_reference_mask;
          for (auto source_index = 0; source_index < sources.size(); ++source_index) {
            auto source = sources.at(source_index).toMap();
            if (color_reference_mask.isEmpty() ||
                normalized_mask_operation(source.value(QStringLiteral("operation")).toString()) ==
                    QStringLiteral("add")) {
              color_reference_mask = source;
            }
            QVariantList single_source;
            single_source.append(source);
            source.insert(
                QStringLiteral("thumbnailSource"),
                thumbnail_source_for_masks(single_source, source));
            sources[source_index] = source;
            thumbnail_masks.append(source);
          }
          set.insert(QStringLiteral("sources"), sources);
          set.insert(
              QStringLiteral("thumbnailSource"),
              thumbnail_source_for_masks(
                  thumbnail_masks,
                  color_reference_mask.isEmpty() && !sources.isEmpty()
                      ? sources.constFirst().toMap()
                      : color_reference_mask));
          cached_mask_sets_[set_index] = set;
        }
      }
    }
    mask_group_cache_dirty_ = false;
  }
  return cached_mask_sets_;
}
int EditorPreviewController::maskSetCount() const { return static_cast<int>(maskSets().size()); }
QVariantList EditorPreviewController::retouchStack() const {
  if (mask_group_cache_dirty_) {
    cached_mask_sets_ = grouped_mask_sets(mask_stack_);
    cached_retouch_stack_ = grouped_retouch_stack(mask_stack_);
    mask_group_cache_dirty_ = false;
  }
  return cached_retouch_stack_;
}
int EditorPreviewController::retouchCount() const { return static_cast<int>(retouchStack().size()); }
QVariantList EditorPreviewController::photoLayers() const { return photo_layers_; }
int EditorPreviewController::photoLayerCount() const { return static_cast<int>(photo_layers_.size()); }
QVariantList EditorPreviewController::photoLayerVisibilityStates() const {
  return photo_layer_visibility_states_;
}
int EditorPreviewController::photoLayerVisibilityStateCount() const {
  return static_cast<int>(photo_layer_visibility_states_.size());
}
int EditorPreviewController::sourceImageWidth() const {
  if (last_render_source_size_.width() > 0) {
    return last_render_source_size_.width();
  }
  return sourceImageSize().width();
}

int EditorPreviewController::sourceImageHeight() const {
  if (last_render_source_size_.height() > 0) {
    return last_render_source_size_.height();
  }
  return sourceImageSize().height();
}

bool EditorPreviewController::photoLayerSoloActive() const {
  for (const auto& value : photo_layers_) {
    if (value.toMap().value(QStringLiteral("solo")).toBool()) {
      return true;
    }
  }
  return false;
}
QVariantList EditorPreviewController::maskStack() const { return mask_stack_; }
int EditorPreviewController::maskCount() const { return static_cast<int>(mask_stack_.size()); }
QVariantList EditorPreviewController::reusableMaskOutputs() const {
  QVariantList outputs;
  auto ordinal = 1;
  for (const auto& value : mask_stack_) {
    const auto mask = normalized_mask_definition(value.toMap(), ordinal);
    ++ordinal;
    if (!mask.value(QStringLiteral("reusableOutput")).toBool()) {
      continue;
    }
    QVariantMap output;
    output.insert(QStringLiteral("id"), mask.value(QStringLiteral("outputId")).toString());
    output.insert(QStringLiteral("maskId"), mask.value(QStringLiteral("id")).toString());
    output.insert(QStringLiteral("name"), mask.value(QStringLiteral("name")).toString());
    output.insert(QStringLiteral("kind"), mask.value(QStringLiteral("kind")).toString());
    output.insert(QStringLiteral("parentId"), mask.value(QStringLiteral("parentId")).toString());
    output.insert(
        QStringLiteral("dependencyLabel"),
        mask.value(QStringLiteral("outputId")).toString().isEmpty()
            ? QStringLiteral("Reusable matte")
            : QStringLiteral("Reusable matte: %1").arg(mask.value(QStringLiteral("outputId")).toString()));
    outputs.append(output);
  }
  return outputs;
}
int EditorPreviewController::activeMaskIndex() const { return active_mask_index_; }
bool EditorPreviewController::hasActiveMask() const {
  return active_mask_index_ >= 0 && active_mask_index_ < mask_stack_.size();
}
QString EditorPreviewController::activeMaskKind() const {
  return activeMaskDefinition().value(QStringLiteral("kind")).toString();
}
QString EditorPreviewController::activeMaskToolRole() const {
  return activeMaskDefinition().value(QStringLiteral("toolRole")).toString();
}
QString EditorPreviewController::activeMaskRetouchMode() const {
  return activeMaskDefinition().value(QStringLiteral("retouchMode")).toString().trimmed().toLower();
}
bool EditorPreviewController::activeMaskCommitted() const {
  return map_bool_value(activeMaskDefinition(), QStringLiteral("committed"), true);
}
QString EditorPreviewController::activeMaskName() const {
  return activeMaskDefinition().value(QStringLiteral("name")).toString();
}
QString EditorPreviewController::activeMaskOperation() const {
  return activeMaskDefinition().value(QStringLiteral("operation")).toString();
}
bool EditorPreviewController::activeMaskEnabled() const {
  return map_bool_value(activeMaskDefinition(), QStringLiteral("enabled"), false);
}
bool EditorPreviewController::activeMaskSolo() const {
  return map_bool_value(activeMaskDefinition(), QStringLiteral("solo"), false);
}
bool EditorPreviewController::activeMaskLocked() const {
  return map_bool_value(activeMaskDefinition(), QStringLiteral("locked"), false);
}
double EditorPreviewController::activeMaskOpacity() const {
  return qBound(0.0, map_double_value(activeMaskDefinition(), QStringLiteral("opacity"), 1.0), 1.0);
}
double EditorPreviewController::activeMaskDensity() const {
  return qBound(0.0, map_double_value(activeMaskDefinition(), QStringLiteral("density"), 1.0), 1.0);
}
bool EditorPreviewController::activeMaskInverted() const {
  const auto mask = activeMaskDefinition();
  return map_bool_value(
      mask,
      QStringLiteral("invert"),
      map_bool_value(mask, QStringLiteral("selectionInvert"), false));
}
QString EditorPreviewController::activeMaskBlendMode() const {
  return normalized_mask_blend_mode(
      activeMaskDefinition().value(QStringLiteral("blendMode")).toString());
}
double EditorPreviewController::activeMaskExposure() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("exposure"), 0.0);
}
double EditorPreviewController::activeMaskContrast() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("contrast"), 0.0);
}
double EditorPreviewController::activeMaskHighlights() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("highlights"), 0.0);
}
double EditorPreviewController::activeMaskShadows() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("shadows"), 0.0);
}
double EditorPreviewController::activeMaskWhites() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("whites"), 0.0);
}
double EditorPreviewController::activeMaskBlacks() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("blacks"), 0.0);
}
double EditorPreviewController::activeMaskTemperature() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("temperature"), 0.0);
}
double EditorPreviewController::activeMaskTint() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("tint"), 0.0);
}
double EditorPreviewController::activeMaskVibrance() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("vibrance"), 0.0);
}
double EditorPreviewController::activeMaskSaturation() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("saturation"), 0.0);
}
double EditorPreviewController::activeMaskHueShift() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("hueShift"), 0.0);
}
double EditorPreviewController::activeMaskSaturationShift() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("saturationShift"), 0.0);
}
double EditorPreviewController::activeMaskColorizeHue() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("colorizeHue"), 0.0);
}
double EditorPreviewController::activeMaskColorizeStrength() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("colorizeStrength"), 0.0);
}
double EditorPreviewController::activeMaskRemoveStrength() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("removeStrength"), 0.0);
}
bool EditorPreviewController::activeMaskSelectionInverted() const {
  return activeMaskInverted();
}
double EditorPreviewController::activeMaskSelectionExpand() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("selectionExpand"), 0.0);
}
double EditorPreviewController::activeMaskSelectionSmooth() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("selectionSmooth"), 0.0);
}
double EditorPreviewController::activeMaskSelectionFineEdge() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("selectionFineEdge"), 0.0);
}
double EditorPreviewController::activeMaskSelectionEdgeContrast() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("selectionEdgeContrast"), 0.0);
}
double EditorPreviewController::activeMaskSelectionDecontaminate() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("selectionDecontaminate"), 0.0);
}
double EditorPreviewController::activeMaskFeather() const {
  const auto mask = activeMaskDefinition();
  return map_double_value(
      mask, QStringLiteral("feather"),
      activeMaskKind() == QStringLiteral("radial") ? 0.25
      : activeMaskKind() == QStringLiteral("brush") ? 0.75
      : activeMaskKind() == QStringLiteral("polygon") ? 0.06
      : activeMaskKind() == QStringLiteral("subject") || activeMaskKind() == QStringLiteral("object") ? 0.5
      : 0.22);
}
double EditorPreviewController::activeMaskPosition() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("position"), 0.5);
}
double EditorPreviewController::activeMaskRotation() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("rotation"), 0.0);
}
double EditorPreviewController::activeMaskCenterX() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("centerX"), 0.5);
}
double EditorPreviewController::activeMaskCenterY() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("centerY"), 0.5);
}
double EditorPreviewController::activeMaskRadiusX() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("radiusX"), 0.25);
}
double EditorPreviewController::activeMaskRadiusY() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("radiusY"), 0.25);
}
double EditorPreviewController::activeMaskBrushSize() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("brushSize"), 0.08);
}
double EditorPreviewController::activeMaskBrushFlow() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("brushFlow"), 1.0);
}
double EditorPreviewController::activeMaskBrushHardness() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("brushHardness"), 0.25);
}
double EditorPreviewController::activeMaskBrushSpacing() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("brushSpacing"), 0.75);
}
double EditorPreviewController::activeMaskBrushSmoothing() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("brushSmoothing"), 0.0);
}
bool EditorPreviewController::activeMaskBrushBuildup() const {
  return map_bool_value(activeMaskDefinition(), QStringLiteral("brushBuildup"), true);
}
bool EditorPreviewController::activeMaskBrushEraseMode() const {
  return map_bool_value(activeMaskDefinition(), QStringLiteral("brushEraseMode"), false);
}
bool EditorPreviewController::activeMaskBrushAutoMask() const {
  return map_bool_value(activeMaskDefinition(), QStringLiteral("brushAutoMask"), false);
}
double EditorPreviewController::activeMaskBrushAutoMaskTolerance() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("brushAutoMaskTolerance"), 0.18);
}
QString EditorPreviewController::activeMaskBrushPreset() const {
  const auto preset = activeMaskDefinition().value(QStringLiteral("brushPreset")).toString().simplified();
  return preset.isEmpty() ? QStringLiteral("Soft Round") : preset;
}
QStringList EditorPreviewController::brushPresetNames() const {
  QStringList names;
  names.reserve(static_cast<qsizetype>(brush_presets().size()));
  for (const auto& preset : brush_presets()) {
    names.push_back(preset.name);
  }
  return names;
}
int EditorPreviewController::activeMaskBrushPointCount() const {
  auto point_count =
      static_cast<int>(activeMaskDefinition().value(QStringLiteral("brushPoints")).toList().size());
  if (brush_stroke_active_ && activeMaskKind() == QStringLiteral("brush")) {
    point_count += static_cast<int>(active_brush_live_points_.size());
  }
  return point_count;
}
QVariantList EditorPreviewController::activeMaskLiveBrushPoints() const {
  if (!brush_stroke_active_ || activeMaskKind() != QStringLiteral("brush")) {
    return {};
  }
  QVariantList points;
  points.reserve(active_brush_live_points_.size());
  for (const auto& point : active_brush_live_points_) {
    points.append(brush_point_variant(point));
  }
  return points;
}
QString EditorPreviewController::activeMaskLiveBrushPreviewSource() const {
  if (!brush_stroke_active_ || activeMaskKind() != QStringLiteral("brush") ||
      activeMaskLiveBrushPreviewNativeAvailable() ||
      active_brush_live_preview_image_.isNull() || image_provider_ == nullptr) {
    return {};
  }
  return with_preview_revision(
      QStringLiteral("image://editor-preview/%1").arg(QString::fromLatin1(kActiveBrushLivePreviewImageId)),
      active_brush_live_preview_revision_);
}
bool EditorPreviewController::activeMaskLiveBrushPreviewNativeAvailable() const {
  return brush_stroke_active_ &&
         activeMaskKind() == QStringLiteral("brush") &&
         brush_preview_compositor_ != nullptr &&
         brush_preview_compositor_->hasNativeTexture() &&
         brush_preview_compositor_->nativeTextureHandle() != 0 &&
         brush_preview_compositor_->nativeTextureSize().isValid();
}
quint64 EditorPreviewController::activeMaskLiveBrushPreviewNativeHandle() const {
  return brush_preview_compositor_ != nullptr ? brush_preview_compositor_->nativeTextureHandle() : 0;
}
QSize EditorPreviewController::activeMaskLiveBrushPreviewNativeSize() const {
  return brush_preview_compositor_ != nullptr ? brush_preview_compositor_->nativeTextureSize() : QSize{};
}
int EditorPreviewController::activeMaskLiveBrushPreviewRevision() const {
  return active_brush_live_preview_revision_;
}
bool EditorPreviewController::brushStrokeActive() const {
  return brush_stroke_active_;
}
bool EditorPreviewController::activeMaskCloneSourceSet() const {
  return map_bool_value(activeMaskDefinition(), QStringLiteral("cloneSourceSet"), false);
}
double EditorPreviewController::activeMaskCloneSourceX() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("cloneSourceX"), 0.5);
}
double EditorPreviewController::activeMaskCloneSourceY() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("cloneSourceY"), 0.5);
}
double EditorPreviewController::activeMaskCloneScale() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("cloneScale"), 1.0);
}
double EditorPreviewController::activeMaskCloneRotation() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("cloneRotation"), 0.0);
}
bool EditorPreviewController::hasSensorDustMap() const {
  return QFileInfo::exists(sensor_dust_map_path(camera_label_, lens_model_));
}
QVariantList EditorPreviewController::activeMaskPolygonPoints() const {
  return activeMaskDefinition().value(QStringLiteral("polygonPoints")).toList();
}
int EditorPreviewController::activeMaskPolygonPointCount() const {
  return static_cast<int>(activeMaskPolygonPoints().size());
}
QVariantList EditorPreviewController::activeMaskOutlinePoints() const {
  const auto outline_paths = activeMaskOutlinePaths();
  if (outline_paths.isEmpty()) {
    return {};
  }
  const auto first_path = outline_paths.first().toList();
  if (!first_path.isEmpty()) {
    return first_path;
  }
  const auto first_value = outline_paths.first();
  if (first_value.metaType().id() == QMetaType::QVariantMap) {
    return outline_paths;
  }
  return {};
}
QVariantList EditorPreviewController::activeMaskOutlinePaths() const {
  const auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return {};
  }

  if (activeMaskToolRole() != QStringLiteral("selection") || file_path_.isEmpty()) {
    return outline_paths_for_mask(mask);
  }

  QVariantList preview_masks;
  auto ordinal = 1;
  const auto set_id = mask.value(QStringLiteral("maskSetId")).toString();
  for (const auto& value : mask_stack_) {
    const auto candidate = normalized_mask_definition(value.toMap(), ordinal);
    ++ordinal;
    if (candidate.value(QStringLiteral("toolRole")).toString() != QStringLiteral("selection")) {
      continue;
    }
    if (!set_id.isEmpty() && candidate.value(QStringLiteral("maskSetId")).toString() != set_id) {
      continue;
    }
    preview_masks.append(candidate);
  }
  if (preview_masks.size() <= 1) {
    return outline_paths_for_mask(mask);
  }

  QImage source_image;
  if (!decodeSourceImage(&source_image, 512)) {
    return outline_paths_for_mask(mask);
  }

  std::vector<photo_editor::pipeline::LocalMask> composite_masks;
  composite_masks.reserve(static_cast<std::size_t>(preview_masks.size()));
  for (const auto& preview_mask_value : preview_masks) {
    composite_masks.push_back(pipeline_local_mask(preview_mask_value.toMap()));
  }
  const auto state = currentState();
  const auto cropped_image = applyCrop(source_image);
  const auto exposure_gain = std::pow(2.0, state.exposure);
  const auto white_balance = preview_white_balance_multipliers(state.temperature, state.tint);
  const auto composite_paths =
      composite_mask_outline_paths(cropped_image, composite_masks, exposure_gain, white_balance);
  if (!composite_paths.isEmpty()) {
    return composite_paths;
  }
  return outline_paths_for_mask(mask);
}
QString EditorPreviewController::activeMaskPreviewSource() const {
  const auto mask = activeMaskDefinition();
  if (mask.isEmpty() || file_path_.isEmpty()) {
    active_mask_preview_cache_key_.clear();
    active_mask_preview_cache_source_.clear();
    return {};
  }

  const auto state = currentState();
  QVariantList preview_masks;
  QVariantMap color_reference_mask = mask;
  const auto kind = normalized_mask_kind(mask.value(QStringLiteral("kind")).toString());
  if (activeMaskToolRole() == QStringLiteral("selection")) {
    const auto set_id = mask.value(QStringLiteral("maskSetId")).toString();
    auto ordinal = 1;
    for (const auto& value : mask_stack_) {
      const auto candidate = normalized_mask_definition(value.toMap(), ordinal);
      ++ordinal;
      if (!is_selection_mask(candidate)) {
        continue;
      }
      if (!set_id.isEmpty() && candidate.value(QStringLiteral("maskSetId")).toString() != set_id) {
        continue;
      }
      preview_masks.append(candidate);
      const auto operation =
          normalized_mask_operation(candidate.value(QStringLiteral("operation")).toString());
      if (color_reference_mask.isEmpty() || operation == QStringLiteral("add")) {
        color_reference_mask = candidate;
      }
    }
  }
  if (preview_masks.isEmpty()) {
    preview_masks.append(mask);
  }

  QVariantMap cache_payload;
  cache_payload.insert(QStringLiteral("filePath"), file_path_);
  const QFileInfo source_info(file_path_);
  cache_payload.insert(
      QStringLiteral("sourceKey"),
      decoded_image_cache_key(source_info, 512) +
          (raw_image_supported(file_path_) ? QStringLiteral("|raw|") + raw_decode_settings_key(state)
                                           : QString()));
  cache_payload.insert(QStringLiteral("cropX"), state.crop_rect_x);
  cache_payload.insert(QStringLiteral("cropY"), state.crop_rect_y);
  cache_payload.insert(QStringLiteral("cropW"), state.crop_rect_width);
  cache_payload.insert(QStringLiteral("cropH"), state.crop_rect_height);
  cache_payload.insert(QStringLiteral("cropZoom"), state.crop_zoom);
  cache_payload.insert(QStringLiteral("cropPreset"), state.crop_preset);
  cache_payload.insert(QStringLiteral("perspectiveVertical"), state.perspective_vertical);
  cache_payload.insert(QStringLiteral("perspectiveHorizontal"), state.perspective_horizontal);
  cache_payload.insert(QStringLiteral("transformRotation"), state.transform_rotation);
  cache_payload.insert(QStringLiteral("transformScale"), state.transform_scale);
  cache_payload.insert(QStringLiteral("transformSkewX"), state.transform_skew_x);
  cache_payload.insert(QStringLiteral("transformSkewY"), state.transform_skew_y);
  cache_payload.insert(QStringLiteral("liquifyCenterX"), state.liquify_center_x);
  cache_payload.insert(QStringLiteral("liquifyCenterY"), state.liquify_center_y);
  cache_payload.insert(QStringLiteral("liquifyRadius"), state.liquify_radius);
  cache_payload.insert(QStringLiteral("liquifyStrength"), state.liquify_strength);
  cache_payload.insert(QStringLiteral("contentAwareCropFill"), state.content_aware_crop_fill);
  cache_payload.insert(QStringLiteral("exposure"), state.exposure);
  cache_payload.insert(QStringLiteral("temperature"), state.temperature);
  cache_payload.insert(QStringLiteral("tint"), state.tint);
  cache_payload.insert(QStringLiteral("previewMode"), maskPreviewMode());
  cache_payload.insert(QStringLiteral("previewMasks"), preview_masks);
  cache_payload.insert(QStringLiteral("colorReference"), color_reference_mask);
  const auto cache_key = QString::fromLatin1(
      QCryptographicHash::hash(
          QJsonDocument::fromVariant(cache_payload).toJson(QJsonDocument::Compact),
          QCryptographicHash::Sha1)
          .toHex());
  if (cache_key == active_mask_preview_cache_key_) {
    return active_mask_preview_cache_source_;
  }

  QImage source_image;
  if (!decodeSourceImage(&source_image, 512)) {
    active_mask_preview_cache_key_ = cache_key;
    active_mask_preview_cache_source_.clear();
    return {};
  }
  const auto cropped_image = applyCrop(source_image);
  const auto exposure_gain = std::pow(2.0, state.exposure);
  const auto white_balance = preview_white_balance_multipliers(state.temperature, state.tint);

  if (activeMaskToolRole() == QStringLiteral("selection")) {
    std::vector<photo_editor::pipeline::LocalMask> composite_masks;
    composite_masks.reserve(static_cast<std::size_t>(preview_masks.size()));
    for (const auto& preview_mask_value : preview_masks) {
      composite_masks.push_back(pipeline_local_mask(preview_mask_value.toMap()));
    }
    active_mask_preview_cache_source_ = composite_mask_preview_source(
        cropped_image,
        composite_masks,
        color_reference_mask,
        maskPreviewMode(),
        exposure_gain,
        white_balance,
        0.48);
  } else if (kind == QStringLiteral("luminance") || kind == QStringLiteral("color") ||
             kind == QStringLiteral("depth")) {
    active_mask_preview_cache_source_ = range_mask_preview_source(
        cropped_image,
        pipeline_local_mask(mask),
        mask,
        maskPreviewMode(),
        exposure_gain,
        white_balance,
        0.48);
  } else {
    active_mask_preview_cache_source_ =
        segmentation_mask_preview_source(mask, maskPreviewMode(), 0.48);
  }
  active_mask_preview_cache_key_ = cache_key;
  return active_mask_preview_cache_source_;
}
QString EditorPreviewController::maskPreviewMode() const {
  return mask_preview_mode_;
}
bool EditorPreviewController::smartSelectPreviewActive() const {
  return !smart_select_preview_mask_.isEmpty();
}
QString EditorPreviewController::smartSelectPreviewSource() const {
  if (smart_select_preview_mask_.isEmpty()) {
    smart_select_preview_cache_key_.clear();
    smart_select_preview_cache_source_.clear();
    return {};
  }
  const auto cache_key = QString::fromLatin1(
      QCryptographicHash::hash(
          QJsonDocument::fromVariant(QVariantMap{
                                         {QStringLiteral("mask"), smart_select_preview_mask_},
                                         {QStringLiteral("previewMode"), maskPreviewMode()},
                                     })
              .toJson(QJsonDocument::Compact),
          QCryptographicHash::Sha1)
          .toHex());
  if (cache_key == smart_select_preview_cache_key_) {
    return smart_select_preview_cache_source_;
  }
  smart_select_preview_cache_key_ = cache_key;
  smart_select_preview_cache_source_ =
      segmentation_mask_preview_source(smart_select_preview_mask_, maskPreviewMode(), 0.40);
  return smart_select_preview_cache_source_;
}
QVariantList EditorPreviewController::smartSelectPreviewOutlinePoints() const {
  return outline_points_for_mask(smart_select_preview_mask_);
}
int EditorPreviewController::smartSelectCandidateCount() const {
  return static_cast<int>(smart_select_preview_candidate_instance_ids_.size());
}
int EditorPreviewController::smartSelectCandidateIndex() const {
  if (smart_select_preview_candidate_instance_ids_.isEmpty()) {
    return 0;
  }
  return qBound(
      0,
      smart_select_preview_candidate_index_,
      static_cast<int>(smart_select_preview_candidate_instance_ids_.size()) - 1);
}
bool EditorPreviewController::smartSelectCanCycleCandidates() const {
  return smart_select_preview_candidate_instance_ids_.size() > 1;
}
double EditorPreviewController::activeMaskLuminanceMin() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("luminanceMin"), 0.2);
}
double EditorPreviewController::activeMaskLuminanceMax() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("luminanceMax"), 0.8);
}
double EditorPreviewController::activeMaskHueCenter() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("hueCenter"), 0.08);
}
double EditorPreviewController::activeMaskHueRange() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("hueRange"), 0.12);
}
double EditorPreviewController::activeMaskSaturationMin() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("saturationMin"), 0.1);
}
double EditorPreviewController::activeMaskSaturationMax() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("saturationMax"), 1.0);
}
double EditorPreviewController::activeMaskDepthMin() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("depthMin"), 0.2);
}
double EditorPreviewController::activeMaskDepthMax() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("depthMax"), 0.8);
}
double EditorPreviewController::activeMaskRangeFeather() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("rangeFeather"), 0.1);
}
double EditorPreviewController::activeMaskRefineLuminanceMin() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("refineLuminanceMin"), 0.0);
}
double EditorPreviewController::activeMaskRefineLuminanceMax() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("refineLuminanceMax"), 1.0);
}
double EditorPreviewController::activeMaskRefineHueCenter() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("refineHueCenter"), 0.0);
}
double EditorPreviewController::activeMaskRefineHueRange() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("refineHueRange"), 0.5);
}
double EditorPreviewController::activeMaskRefineSaturationMin() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("refineSaturationMin"), 0.0);
}
double EditorPreviewController::activeMaskRefineSaturationMax() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("refineSaturationMax"), 1.0);
}
double EditorPreviewController::activeMaskRefineDepthMin() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("refineDepthMin"), 0.0);
}
double EditorPreviewController::activeMaskRefineDepthMax() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("refineDepthMax"), 1.0);
}
double EditorPreviewController::activeMaskRefineEdgeConfidenceMin() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("refineEdgeConfidenceMin"), 0.0);
}
double EditorPreviewController::activeMaskRefineRangeFeather() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("refineRangeFeather"), 0.05);
}
bool EditorPreviewController::whiteBalanceSampling() const { return white_balance_sampling_; }
bool EditorPreviewController::linearMaskEnabled() const { return linear_mask_enabled_; }
double EditorPreviewController::linearMaskExposure() const { return linear_mask_exposure_; }
double EditorPreviewController::linearMaskTemperature() const { return linear_mask_temperature_; }
double EditorPreviewController::linearMaskFeather() const { return linear_mask_feather_; }
double EditorPreviewController::linearMaskPosition() const { return linear_mask_position_; }
double EditorPreviewController::linearMaskRotation() const { return linear_mask_rotation_; }
bool EditorPreviewController::radialMaskEnabled() const { return radial_mask_enabled_; }
double EditorPreviewController::radialMaskExposure() const { return radial_mask_exposure_; }
double EditorPreviewController::radialMaskTemperature() const { return radial_mask_temperature_; }
double EditorPreviewController::radialMaskFeather() const { return radial_mask_feather_; }
double EditorPreviewController::radialMaskCenterX() const { return radial_mask_center_x_; }
double EditorPreviewController::radialMaskCenterY() const { return radial_mask_center_y_; }
double EditorPreviewController::radialMaskRadiusX() const { return radial_mask_radius_x_; }
double EditorPreviewController::radialMaskRadiusY() const { return radial_mask_radius_y_; }
double EditorPreviewController::radialMaskRotation() const { return radial_mask_rotation_; }

void EditorPreviewController::setPreviewTargetEdge(int edge) {
  const auto clamped_edge = clamp_preview_target_edge(edge);
  if (previewTargetEdge() == clamped_edge) {
    return;
  }

  preview_target_edge_ = clamped_edge;
  emit previewTargetEdgeChanged();
  if (hasAsset()) {
    renderPreview(false);
  }
}

void EditorPreviewController::setPresetIntensity(double intensity) {
  const auto clamped_intensity = qBound(0.0, intensity, 1.0);
  if (std::abs(preset_intensity_ - clamped_intensity) <= 1e-6) {
    return;
  }
  preset_intensity_ = clamped_intensity;
  emit presetIntensityChanged();
}

void EditorPreviewController::setPreviewViewport(
    double normalized_x,
    double normalized_y,
    double normalized_width,
    double normalized_height,
    int patch_max_edge,
    bool active) {
  const auto clamped_width = qBound(0.0, normalized_width, 1.0);
  const auto clamped_height = qBound(0.0, normalized_height, 1.0);
  const auto next_rect = active && clamped_width > 0.0 && clamped_height > 0.0
                             ? QRectF(
                                   qBound(0.0, normalized_x, 1.0 - clamped_width),
                                   qBound(0.0, normalized_y, 1.0 - clamped_height),
                                   clamped_width,
                                   clamped_height)
                             : QRectF();
  const auto next_patch_edge = std::max(0, patch_max_edge);

  if (preview_viewport_active_ == active &&
      preview_viewport_rect_ == next_rect &&
      preview_viewport_patch_max_edge_ == next_patch_edge) {
    return;
  }

  preview_viewport_active_ = active && next_rect.isValid();
  preview_viewport_rect_ = preview_viewport_active_ ? next_rect : QRectF();
  preview_viewport_patch_max_edge_ = preview_viewport_active_ ? next_patch_edge : 0;
  if (!hasAsset()) {
    return;
  }

  if (!preview_viewport_active_) {
    if (interactive_request_pending_ && pending_interactive_request_.overlay_only) {
      interactive_request_pending_ = false;
    }
    display_preview_overlay_source_.clear();
    display_reference_preview_overlay_source_.clear();
    display_preview_overlay_rect_ = QRectF();
    if (image_provider_ != nullptr) {
      image_provider_->clearImage(QString::fromLatin1(kEditedPreviewOverlayImageId));
      image_provider_->clearImage(QString::fromLatin1(kReferencePreviewOverlayImageId));
    }
    emit previewChanged();
    return;
  }

  ++overlay_generation_;
  latest_overlay_generation_->store(overlay_generation_, std::memory_order_relaxed);
  auto request = currentRenderRequest(false);
  request.overlay_only = true;
  request.generation = overlay_generation_;
  request.latest_generation = latest_overlay_generation_;

  if (interactive_render_watcher_.isRunning()) {
    pending_interactive_request_ = request;
    interactive_request_pending_ = true;
    updateProcessingState();
    return;
  }

  startRenderStage(false, request);
}

void EditorPreviewController::setCompareMode(bool compare_mode) {
  if (compare_mode_ == compare_mode) {
    return;
  }
  compare_mode_ = compare_mode;
  if (compare_mode_ && crop_editing_) {
    crop_editing_ = false;
    emit controlsChanged();
  }
  emit compareModeChanged();
  if (compare_mode_ && hasAsset()) {
    renderPreview(false, QStringLiteral("compare_toggle"));
  }
}

void EditorPreviewController::setCompareAdjustmentGroups(const QStringList& groups) {
  const auto normalized_groups = normalized_preset_adjustment_groups(groups);
  if (compare_adjustment_groups_ == normalized_groups) {
    return;
  }
  compare_adjustment_groups_ = normalized_groups;
  emit compareAdjustmentGroupsChanged();
  if (compare_mode_ && hasAsset() && reference_image_path_.isEmpty()) {
    renderPreview(false, QStringLiteral("compare_adjustment_change"));
  }
}

bool EditorPreviewController::setReferenceImage(const QString& file_path) {
  const auto normalized_path = local_file_path_from_user_input(file_path);
  if (normalized_path.isEmpty()) {
    setStatus(QStringLiteral("Choose a reference image file."));
    return false;
  }

  const QFileInfo info(normalized_path);
  if (!info.exists() || !info.isReadable() || !info.isFile()) {
    setStatus(QStringLiteral("Reference image unavailable."));
    return false;
  }
  QImageReader reader(normalized_path);
  reader.setAutoTransform(true);
  const auto qt_readable = reader.canRead();
  if (!qt_readable && !native_image_supported(normalized_path)) {
    setStatus(QStringLiteral("Reference image format is not supported."));
    return false;
  }

  if (reference_image_path_ == normalized_path) {
    return true;
  }
  reference_image_path_ = normalized_path;
  reference_image_name_ = info.fileName();
  reference_preview_source_.clear();
  display_reference_preview_source_.clear();
  display_reference_preview_overlay_source_.clear();
  setStatus(QStringLiteral("Reference image set."));
  emit referenceImageChanged();
  emit previewChanged();
  if (compare_mode_ && hasAsset()) {
    renderPreview(false, QStringLiteral("reference_change"));
  }
  return true;
}

void EditorPreviewController::clearReferenceImage() {
  if (reference_image_path_.isEmpty()) {
    return;
  }
  reference_image_path_.clear();
  reference_image_name_.clear();
  reference_preview_source_.clear();
  display_reference_preview_source_.clear();
  display_reference_preview_overlay_source_.clear();
  setStatus(QStringLiteral("Reference image cleared."));
  emit referenceImageChanged();
  emit previewChanged();
  if (compare_mode_ && hasAsset()) {
    renderPreview(false, QStringLiteral("reference_change"));
  }
}

void EditorPreviewController::setRetouchBeforeAfterEnabled(bool enabled) {
  if (retouch_before_after_enabled_ == enabled) {
    return;
  }

  retouch_before_after_enabled_ = enabled;
  emit controlsChanged();
  if (hasAsset()) {
    renderPreview();
  }
}

void EditorPreviewController::setInpaintPatchDiagnosticsEnabled(bool enabled) {
  if (inpaint_patch_diagnostics_enabled_ == enabled) {
    return;
  }

  inpaint_patch_diagnostics_enabled_ = enabled;
  emit controlsChanged();
  emit maskOverlayChanged();
}

void EditorPreviewController::setMaskPreviewMode(const QString& mode) {
  const auto normalized_mode = mask_preview_mode_to_string(mask_preview_mode_from_string(mode));
  if (mask_preview_mode_ == normalized_mode) {
    return;
  }

  mask_preview_mode_ = normalized_mode;
  active_mask_preview_cache_key_.clear();
  active_mask_preview_cache_source_.clear();
  smart_select_preview_cache_key_.clear();
  smart_select_preview_cache_source_.clear();
  emit controlsChanged();
  emit maskOverlayChanged();
  emit activeBrushStrokeChanged();
  emit smartSelectPreviewChanged();
}

void EditorPreviewController::setCropEditing(bool crop_editing) {
  if (crop_editing_ == crop_editing) {
    return;
  }
  crop_editing_ = crop_editing;
  if (crop_editing_) {
    if (compare_mode_) {
      compare_mode_ = false;
      emit compareModeChanged();
    }
    if (white_balance_sampling_) {
      white_balance_sampling_ = false;
      emit whiteBalanceSamplingChanged();
    }
  }
  emit controlsChanged();
  if (hasAsset()) {
    renderPreview(false, QStringLiteral("crop_change"));
  }
}

void EditorPreviewController::setInputColorProfile(const QString& input_color_profile) {
  const auto normalized = normalized_input_color_profile_name(input_color_profile);
  if (input_color_profile_ == normalized) {
    return;
  }
  rememberStateForUndo();
  input_color_profile_ = normalized;
  persistCurrentState();
  emit controlsChanged();
  renderPreview(true, QStringLiteral("output_profile_change"));
}

void EditorPreviewController::setRawExposure(double raw_exposure) {
  raw_exposure = qBound(-4.0, raw_exposure, 4.0);
  if (qFuzzyCompare(raw_exposure_ + 1.0, raw_exposure + 1.0)) {
    return;
  }
  rememberStateForUndo();
  raw_exposure_ = raw_exposure;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setOutputColorSpace(const QString& output_color_space) {
  const auto normalized = normalized_output_color_space_name(output_color_space);
  if (output_color_space_ == normalized) {
    return;
  }
  rememberStateForUndo();
  output_color_space_ = normalized;
  preview_color_space_ = normalized;
  persistCurrentState();
  emit controlsChanged();
  renderPreview(true, QStringLiteral("output_profile_change"));
}

void EditorPreviewController::setMonitorColorProfile(const QString& monitor_color_profile) {
  const auto normalized = normalized_monitor_color_profile_name(monitor_color_profile);
  if (monitor_color_profile_ == normalized) {
    return;
  }
  rememberStateForUndo();
  monitor_color_profile_ = normalized;
  persistCurrentState();
  emit controlsChanged();
  renderPreview(true, QStringLiteral("output_profile_change"));
}

void EditorPreviewController::setSoftProofEnabled(bool enabled) {
  if (soft_proof_enabled_ == enabled) {
    return;
  }
  rememberStateForUndo();
  soft_proof_enabled_ = enabled;
  persistCurrentState();
  emit controlsChanged();
  renderPreview(true, QStringLiteral("output_profile_change"));
}

void EditorPreviewController::setSoftProofProfile(const QString& soft_proof_profile) {
  const auto normalized = normalized_output_color_space_name(soft_proof_profile);
  if (soft_proof_profile_ == normalized) {
    return;
  }
  rememberStateForUndo();
  soft_proof_profile_ = normalized;
  persistCurrentState();
  emit controlsChanged();
  renderPreview(true, QStringLiteral("output_profile_change"));
}

void EditorPreviewController::setRawBoost(double raw_boost) {
  raw_boost = qBound(0.0, raw_boost, 2.0);
  if (qFuzzyCompare(raw_boost_ + 1.0, raw_boost + 1.0)) {
    return;
  }
  rememberStateForUndo();
  raw_boost_ = raw_boost;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setRawShadowBoost(double raw_shadow_boost) {
  raw_shadow_boost = qBound(0.0, raw_shadow_boost, 2.0);
  if (qFuzzyCompare(raw_shadow_boost_ + 1.0, raw_shadow_boost + 1.0)) {
    return;
  }
  rememberStateForUndo();
  raw_shadow_boost_ = raw_shadow_boost;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setRawDetail(double raw_detail) {
  raw_detail = qBound(0.0, raw_detail, 2.0);
  if (qFuzzyCompare(raw_detail_ + 1.0, raw_detail + 1.0)) {
    return;
  }
  rememberStateForUndo();
  raw_detail_ = raw_detail;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setRawChromaNoiseReduction(double raw_chroma_noise_reduction) {
  raw_chroma_noise_reduction = qBound(0.0, raw_chroma_noise_reduction, 1.0);
  if (qFuzzyCompare(raw_chroma_noise_reduction_ + 1.0, raw_chroma_noise_reduction + 1.0)) {
    return;
  }
  rememberStateForUndo();
  raw_chroma_noise_reduction_ = raw_chroma_noise_reduction;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setRawMoireReduction(double raw_moire_reduction) {
  raw_moire_reduction = qBound(0.0, raw_moire_reduction, 1.0);
  if (qFuzzyCompare(raw_moire_reduction_ + 1.0, raw_moire_reduction + 1.0)) {
    return;
  }
  rememberStateForUndo();
  raw_moire_reduction_ = raw_moire_reduction;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setRawHotDeadPixelCleanup(double raw_hot_dead_pixel_cleanup) {
  raw_hot_dead_pixel_cleanup = qBound(0.0, raw_hot_dead_pixel_cleanup, 1.0);
  if (qFuzzyCompare(raw_hot_dead_pixel_cleanup_ + 1.0, raw_hot_dead_pixel_cleanup + 1.0)) {
    return;
  }
  rememberStateForUndo();
  raw_hot_dead_pixel_cleanup_ = raw_hot_dead_pixel_cleanup;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setRawFalseColorSuppression(double raw_false_color_suppression) {
  raw_false_color_suppression = qBound(0.0, raw_false_color_suppression, 1.0);
  if (qFuzzyCompare(raw_false_color_suppression_ + 1.0, raw_false_color_suppression + 1.0)) {
    return;
  }
  rememberStateForUndo();
  raw_false_color_suppression_ = raw_false_color_suppression;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setRawHighlightRecovery(bool enabled) {
  if (raw_highlight_recovery_ == enabled) {
    return;
  }
  rememberStateForUndo();
  raw_highlight_recovery_ = enabled;
  if (!enabled) {
    highlight_reconstruction_mode_ = QStringLiteral("Disabled");
  } else if (highlight_reconstruction_mode_ == QStringLiteral("Disabled")) {
    highlight_reconstruction_mode_ = QStringLiteral("Enabled");
  }
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setHighlightReconstructionMode(const QString& mode) {
  const auto normalized = normalized_highlight_reconstruction_mode_name(mode);
  if (highlight_reconstruction_mode_ == normalized) {
    return;
  }
  rememberStateForUndo();
  highlight_reconstruction_mode_ = normalized;
  raw_highlight_recovery_ = normalized != QStringLiteral("Disabled");
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setRawDemosaicQuality(const QString& quality) {
  const auto normalized = normalized_raw_demosaic_quality_name(quality);
  if (raw_demosaic_quality_ == normalized) {
    return;
  }
  rememberStateForUndo();
  raw_demosaic_quality_ = normalized;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setRawGamutMapping(bool enabled) {
  if (raw_gamut_mapping_ == enabled) {
    return;
  }
  rememberStateForUndo();
  raw_gamut_mapping_ = enabled;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setRawLensCorrection(bool enabled) {
  if (raw_lens_correction_ == enabled) {
    return;
  }
  rememberStateForUndo();
  raw_lens_correction_ = enabled;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setRawWhiteBalanceOverride(bool enabled) {
  if (raw_white_balance_override_ == enabled) {
    return;
  }
  rememberStateForUndo();
  raw_white_balance_override_ = enabled;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setRawCameraMatchingLook(const QString& look) {
  const auto normalized = normalized_camera_matching_look_name(look);
  if (raw_camera_matching_look_ == normalized) {
    return;
  }
  rememberStateForUndo();
  raw_camera_matching_look_ = normalized;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setRawWhiteBalanceTemperature(double temperature) {
  temperature = qBound(2000.0, temperature, 50000.0);
  if (qFuzzyCompare(raw_white_balance_temperature_ + 1.0, temperature + 1.0)) {
    return;
  }
  rememberStateForUndo();
  raw_white_balance_temperature_ = temperature;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setRawWhiteBalanceTint(double tint) {
  tint = qBound(-150.0, tint, 150.0);
  if (qFuzzyCompare(raw_white_balance_tint_ + 1.0, tint + 1.0)) {
    return;
  }
  rememberStateForUndo();
  raw_white_balance_tint_ = tint;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::useAsShotRawWhiteBalance() {
  const auto temperature = qBound(2000.0, raw_as_shot_white_balance_temperature_, 50000.0);
  const auto tint = qBound(-150.0, raw_as_shot_white_balance_tint_, 150.0);
  if (raw_white_balance_override_ &&
      qFuzzyCompare(raw_white_balance_temperature_ + 1.0, temperature + 1.0) &&
      qFuzzyCompare(raw_white_balance_tint_ + 1.0, tint + 1.0)) {
    return;
  }

  rememberStateForUndo();
  raw_white_balance_override_ = true;
  raw_white_balance_temperature_ = temperature;
  raw_white_balance_tint_ = tint;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setRawBlackLevel(double black_level) {
  black_level = qBound(0.0, black_level, 0.95);
  if (qFuzzyCompare(raw_black_level_ + 1.0, black_level + 1.0)) {
    return;
  }
  rememberStateForUndo();
  raw_black_level_ = black_level;
  raw_white_level_ = std::max(raw_white_level_, raw_black_level_ + 0.01);
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setRawWhiteLevel(double white_level) {
  white_level = qBound(raw_black_level_ + 0.01, white_level, 4.0);
  if (qFuzzyCompare(raw_white_level_ + 1.0, white_level + 1.0)) {
    return;
  }
  rememberStateForUndo();
  raw_white_level_ = white_level;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setRawCalibrationHue(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(raw_calibration_hue_ + 1.0, value + 1.0)) {
    return;
  }
  rememberStateForUndo();
  raw_calibration_hue_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setRawCalibrationSaturation(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(raw_calibration_saturation_ + 1.0, value + 1.0)) {
    return;
  }
  rememberStateForUndo();
  raw_calibration_saturation_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setRawRedPrimaryHue(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(raw_red_primary_hue_ + 1.0, value + 1.0)) {
    return;
  }
  rememberStateForUndo();
  raw_red_primary_hue_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setRawRedPrimarySaturation(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(raw_red_primary_saturation_ + 1.0, value + 1.0)) {
    return;
  }
  rememberStateForUndo();
  raw_red_primary_saturation_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setRawGreenPrimaryHue(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(raw_green_primary_hue_ + 1.0, value + 1.0)) {
    return;
  }
  rememberStateForUndo();
  raw_green_primary_hue_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setRawGreenPrimarySaturation(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(raw_green_primary_saturation_ + 1.0, value + 1.0)) {
    return;
  }
  rememberStateForUndo();
  raw_green_primary_saturation_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setRawBluePrimaryHue(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(raw_blue_primary_hue_ + 1.0, value + 1.0)) {
    return;
  }
  rememberStateForUndo();
  raw_blue_primary_hue_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setRawBluePrimarySaturation(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(raw_blue_primary_saturation_ + 1.0, value + 1.0)) {
    return;
  }
  rememberStateForUndo();
  raw_blue_primary_saturation_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setExposure(double exposure) {
  exposure = qBound(-2.0, exposure, 2.0);
  if (qFuzzyCompare(exposure_, exposure)) {
    return;
  }
  rememberStateForUndo();
  exposure_ = exposure;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setContrast(double contrast) {
  contrast = clamp_signed_percent(contrast);
  if (qFuzzyCompare(contrast_ + 1.0, contrast + 1.0)) {
    return;
  }
  rememberStateForUndo();
  contrast_ = contrast;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setHighlights(double highlights) {
  highlights = clamp_signed_percent(highlights);
  if (qFuzzyCompare(highlights_ + 1.0, highlights + 1.0)) {
    return;
  }
  rememberStateForUndo();
  highlights_ = highlights;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setShadows(double shadows) {
  shadows = clamp_signed_percent(shadows);
  if (qFuzzyCompare(shadows_ + 1.0, shadows + 1.0)) {
    return;
  }
  rememberStateForUndo();
  shadows_ = shadows;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setWhites(double whites) {
  whites = clamp_signed_percent(whites);
  if (qFuzzyCompare(whites_ + 1.0, whites + 1.0)) {
    return;
  }
  rememberStateForUndo();
  whites_ = whites;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setBlacks(double blacks) {
  blacks = clamp_signed_percent(blacks);
  if (qFuzzyCompare(blacks_ + 1.0, blacks + 1.0)) {
    return;
  }
  rememberStateForUndo();
  blacks_ = blacks;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setBrightness(double brightness) {
  brightness = clamp_signed_percent(brightness);
  if (qFuzzyCompare(brightness_ + 1.0, brightness + 1.0)) {
    return;
  }
  rememberStateForUndo();
  brightness_ = brightness;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setHdrLocalToneMapping(double hdr_local_tone_mapping) {
  hdr_local_tone_mapping = qBound(0.0, hdr_local_tone_mapping, 100.0);
  if (qFuzzyCompare(hdr_local_tone_mapping_ + 1.0, hdr_local_tone_mapping + 1.0)) {
    return;
  }
  rememberStateForUndo();
  hdr_local_tone_mapping_ = hdr_local_tone_mapping;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setFilmicShoulder(double filmic_shoulder) {
  filmic_shoulder = clamp_signed_percent(filmic_shoulder);
  if (qFuzzyCompare(filmic_shoulder_ + 1.0, filmic_shoulder + 1.0)) {
    return;
  }
  rememberStateForUndo();
  filmic_shoulder_ = filmic_shoulder;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setFilmicToe(double filmic_toe) {
  filmic_toe = clamp_signed_percent(filmic_toe);
  if (qFuzzyCompare(filmic_toe_ + 1.0, filmic_toe + 1.0)) {
    return;
  }
  rememberStateForUndo();
  filmic_toe_ = filmic_toe;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setAdaptiveContrast(double adaptive_contrast) {
  adaptive_contrast = clamp_signed_percent(adaptive_contrast);
  if (qFuzzyCompare(adaptive_contrast_ + 1.0, adaptive_contrast + 1.0)) {
    return;
  }
  rememberStateForUndo();
  adaptive_contrast_ = adaptive_contrast;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setLevelsBlackPoint(double black_point) {
  black_point = qBound(0.0, black_point, 100.0);
  if (qFuzzyCompare(levels_black_point_ + 1.0, black_point + 1.0)) {
    return;
  }
  rememberStateForUndo();
  levels_black_point_ = black_point;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setLevelsGamma(double gamma) {
  gamma = clamp_signed_percent(gamma);
  if (qFuzzyCompare(levels_gamma_ + 1.0, gamma + 1.0)) {
    return;
  }
  rememberStateForUndo();
  levels_gamma_ = gamma;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setLevelsWhitePoint(double white_point) {
  white_point = qBound(0.0, white_point, 100.0);
  if (qFuzzyCompare(levels_white_point_ + 1.0, white_point + 1.0)) {
    return;
  }
  rememberStateForUndo();
  levels_white_point_ = white_point;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setZoneDeepShadows(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(zone_deep_shadows_ + 1.0, value + 1.0)) {
    return;
  }
  rememberStateForUndo();
  zone_deep_shadows_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setZoneShadows(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(zone_shadows_ + 1.0, value + 1.0)) {
    return;
  }
  rememberStateForUndo();
  zone_shadows_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setZoneMidtones(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(zone_midtones_ + 1.0, value + 1.0)) {
    return;
  }
  rememberStateForUndo();
  zone_midtones_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setZoneLights(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(zone_lights_ + 1.0, value + 1.0)) {
    return;
  }
  rememberStateForUndo();
  zone_lights_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setZoneHighlights(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(zone_highlights_ + 1.0, value + 1.0)) {
    return;
  }
  rememberStateForUndo();
  zone_highlights_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setTemperature(double temperature) {
  temperature = clamp_signed_percent(temperature);
  if (qFuzzyCompare(temperature_ + 1.0, temperature + 1.0)) {
    return;
  }
  rememberStateForUndo();
  temperature_ = temperature;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setTint(double tint) {
  tint = clamp_signed_percent(tint);
  if (qFuzzyCompare(tint_ + 1.0, tint + 1.0)) {
    return;
  }
  rememberStateForUndo();
  tint_ = tint;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setTexture(double texture) {
  texture = clamp_signed_percent(texture);
  if (qFuzzyCompare(texture_ + 1.0, texture + 1.0)) {
    return;
  }
  rememberStateForUndo();
  texture_ = texture;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setClarity(double clarity) {
  clarity = clamp_signed_percent(clarity);
  if (qFuzzyCompare(clarity_ + 1.0, clarity + 1.0)) {
    return;
  }
  rememberStateForUndo();
  clarity_ = clarity;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setDehaze(double dehaze) {
  dehaze = clamp_signed_percent(dehaze);
  if (qFuzzyCompare(dehaze_ + 1.0, dehaze + 1.0)) {
    return;
  }
  rememberStateForUndo();
  dehaze_ = dehaze;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setSharpening(double sharpening) {
  sharpening = clamp_signed_percent(sharpening);
  if (qFuzzyCompare(sharpening_ + 1.0, sharpening + 1.0)) {
    return;
  }
  rememberStateForUndo();
  sharpening_ = sharpening;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setSharpenMasking(double sharpen_masking) {
  sharpen_masking = clamp_signed_percent(sharpen_masking);
  if (qFuzzyCompare(sharpen_masking_ + 1.0, sharpen_masking + 1.0)) {
    return;
  }
  rememberStateForUndo();
  sharpen_masking_ = sharpen_masking;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setLuminanceNr(double luminance_nr) {
  luminance_nr = clamp_signed_percent(luminance_nr);
  if (qFuzzyCompare(luminance_nr_ + 1.0, luminance_nr + 1.0)) {
    return;
  }
  rememberStateForUndo();
  luminance_nr_ = luminance_nr;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setColorNr(double color_nr) {
  color_nr = clamp_signed_percent(color_nr);
  if (qFuzzyCompare(color_nr_ + 1.0, color_nr + 1.0)) {
    return;
  }
  rememberStateForUndo();
  color_nr_ = color_nr;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setLensDistortion(double lens_distortion) {
  lens_distortion = clamp_signed_percent(lens_distortion);
  if (qFuzzyCompare(lens_distortion_ + 1.0, lens_distortion + 1.0)) {
    return;
  }
  rememberStateForUndo();
  lens_distortion_ = lens_distortion;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setLensVignette(double lens_vignette) {
  lens_vignette = clamp_signed_percent(lens_vignette);
  if (qFuzzyCompare(lens_vignette_ + 1.0, lens_vignette + 1.0)) {
    return;
  }
  rememberStateForUndo();
  lens_vignette_ = lens_vignette;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setChromaticAberration(double chromatic_aberration) {
  chromatic_aberration = qBound(0.0, chromatic_aberration, 100.0);
  if (qFuzzyCompare(chromatic_aberration_ + 1.0, chromatic_aberration + 1.0)) {
    return;
  }
  rememberStateForUndo();
  chromatic_aberration_ = chromatic_aberration;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setPerspectiveVertical(double perspective_vertical) {
  perspective_vertical = clamp_signed_percent(perspective_vertical);
  if (qFuzzyCompare(perspective_vertical_ + 1.0, perspective_vertical + 1.0)) {
    return;
  }
  rememberStateForUndo();
  perspective_vertical_ = perspective_vertical;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setPerspectiveHorizontal(double perspective_horizontal) {
  perspective_horizontal = clamp_signed_percent(perspective_horizontal);
  if (qFuzzyCompare(perspective_horizontal_ + 1.0, perspective_horizontal + 1.0)) {
    return;
  }
  rememberStateForUndo();
  perspective_horizontal_ = perspective_horizontal;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setTransformRotation(double transform_rotation) {
  transform_rotation = qBound(-45.0, transform_rotation, 45.0);
  if (qFuzzyCompare(transform_rotation_ + 1.0, transform_rotation + 1.0)) {
    return;
  }
  rememberStateForUndo();
  transform_rotation_ = transform_rotation;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setTransformScale(double transform_scale) {
  transform_scale = qBound(0.25, transform_scale, 4.0);
  if (qFuzzyCompare(transform_scale_, transform_scale)) {
    return;
  }
  rememberStateForUndo();
  transform_scale_ = transform_scale;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setTransformSkewX(double transform_skew_x) {
  transform_skew_x = clamp_signed_percent(transform_skew_x);
  if (qFuzzyCompare(transform_skew_x_ + 1.0, transform_skew_x + 1.0)) {
    return;
  }
  rememberStateForUndo();
  transform_skew_x_ = transform_skew_x;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setTransformSkewY(double transform_skew_y) {
  transform_skew_y = clamp_signed_percent(transform_skew_y);
  if (qFuzzyCompare(transform_skew_y_ + 1.0, transform_skew_y + 1.0)) {
    return;
  }
  rememberStateForUndo();
  transform_skew_y_ = transform_skew_y;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setLiquifyCenterX(double liquify_center_x) {
  liquify_center_x = qBound(0.0, liquify_center_x, 1.0);
  if (qFuzzyCompare(liquify_center_x_ + 1.0, liquify_center_x + 1.0)) {
    return;
  }
  rememberStateForUndo();
  liquify_center_x_ = liquify_center_x;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setLiquifyCenterY(double liquify_center_y) {
  liquify_center_y = qBound(0.0, liquify_center_y, 1.0);
  if (qFuzzyCompare(liquify_center_y_ + 1.0, liquify_center_y + 1.0)) {
    return;
  }
  rememberStateForUndo();
  liquify_center_y_ = liquify_center_y;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setLiquifyRadius(double liquify_radius) {
  liquify_radius = qBound(0.02, liquify_radius, 1.0);
  if (qFuzzyCompare(liquify_radius_ + 1.0, liquify_radius + 1.0)) {
    return;
  }
  rememberStateForUndo();
  liquify_radius_ = liquify_radius;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setLiquifyStrength(double liquify_strength) {
  liquify_strength = clamp_signed_percent(liquify_strength);
  if (qFuzzyCompare(liquify_strength_ + 1.0, liquify_strength + 1.0)) {
    return;
  }
  rememberStateForUndo();
  liquify_strength_ = liquify_strength;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setContentAwareCropFill(bool enabled) {
  if (content_aware_crop_fill_ == enabled) {
    return;
  }
  rememberStateForUndo();
  content_aware_crop_fill_ = enabled;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setVibrance(double vibrance) {
  vibrance = clamp_signed_percent(vibrance);
  if (qFuzzyCompare(vibrance_ + 1.0, vibrance + 1.0)) {
    return;
  }
  rememberStateForUndo();
  vibrance_ = vibrance;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setSaturation(double saturation) {
  saturation = clamp_signed_percent(saturation);
  if (qFuzzyCompare(saturation_ + 1.0, saturation + 1.0)) {
    return;
  }
  rememberStateForUndo();
  saturation_ = saturation;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setCurveShadows(double curve_shadows) {
  curve_shadows = clamp_signed_percent(curve_shadows);
  if (qFuzzyCompare(curve_shadows_ + 1.0, curve_shadows + 1.0)) {
    return;
  }
  rememberStateForUndo();
  curve_shadows_ = curve_shadows;
  master_curve_points_.clear();
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setCurveDarks(double curve_darks) {
  curve_darks = clamp_signed_percent(curve_darks);
  if (qFuzzyCompare(curve_darks_ + 1.0, curve_darks + 1.0)) {
    return;
  }
  rememberStateForUndo();
  curve_darks_ = curve_darks;
  master_curve_points_.clear();
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setCurveLights(double curve_lights) {
  curve_lights = clamp_signed_percent(curve_lights);
  if (qFuzzyCompare(curve_lights_ + 1.0, curve_lights + 1.0)) {
    return;
  }
  rememberStateForUndo();
  curve_lights_ = curve_lights;
  master_curve_points_.clear();
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setCurveHighlights(double curve_highlights) {
  curve_highlights = clamp_signed_percent(curve_highlights);
  if (qFuzzyCompare(curve_highlights_ + 1.0, curve_highlights + 1.0)) {
    return;
  }
  rememberStateForUndo();
  curve_highlights_ = curve_highlights;
  master_curve_points_.clear();
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setCurveChannel(const QString& curve_channel) {
  auto normalized = curve_channel.trimmed().isEmpty() ? QStringLiteral("RGB") : curve_channel.trimmed();
  if (normalized == QStringLiteral("Master")) {
    normalized = QStringLiteral("RGB");
  }
  if (curve_channel_ == normalized) {
    return;
  }
  curve_channel_ = normalized;
  emit controlsChanged();
}

void EditorPreviewController::setActiveCurvePoints(const QVariantList& active_curve_points) {
  const auto normalized_points = normalize_curve_points(active_curve_points);
  auto* target_points = &blue_curve_points_;
  if (curve_channel_ == QStringLiteral("RGB") || curve_channel_ == QStringLiteral("Master")) {
    target_points = &master_curve_points_;
  } else if (curve_channel_ == QStringLiteral("Luma")) {
    target_points = &luma_curve_points_;
  } else if (curve_channel_ == QStringLiteral("Red")) {
    target_points = &red_curve_points_;
  } else if (curve_channel_ == QStringLiteral("Green")) {
    target_points = &green_curve_points_;
  }

  if (*target_points == normalized_points) {
    return;
  }

  rememberStateForUndo();
  *target_points = normalized_points;
  if ((curve_channel_ == QStringLiteral("RGB") || curve_channel_ == QStringLiteral("Master")) &&
      !master_curve_points_.isEmpty()) {
    curve_shadows_ = 0.0;
    curve_darks_ = 0.0;
    curve_lights_ = 0.0;
    curve_highlights_ = 0.0;
  }

  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setColorMixerModel(const QString& model) {
  auto normalized = model.trimmed();
  if (normalized.isEmpty()) {
    normalized = QStringLiteral("OKLab");
  }
  if (normalized.compare(QStringLiteral("OKLab"), Qt::CaseInsensitive) == 0) {
    normalized = QStringLiteral("OKLab");
  } else if (normalized.compare(QStringLiteral("LCH"), Qt::CaseInsensitive) == 0 ||
             normalized.compare(QStringLiteral("OKLCH"), Qt::CaseInsensitive) == 0) {
    normalized = QStringLiteral("LCH");
  } else if (normalized.compare(QStringLiteral("HSV"), Qt::CaseInsensitive) == 0) {
    normalized = QStringLiteral("HSV");
  } else {
    normalized = QStringLiteral("HSL");
  }

  if (color_mixer_model_ == normalized) {
    return;
  }

  rememberStateForUndo();
  color_mixer_model_ = normalized;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setHslChannel(const QString& hsl_channel) {
  const auto normalized = hsl_channel.isEmpty() ? QStringLiteral("Reds") : hsl_channel;
  if (hsl_channel_ == normalized) {
    return;
  }
  rememberStateForUndo();
  static const QStringList kMixerChannelNames = {
      QStringLiteral("Reds"), QStringLiteral("Oranges"), QStringLiteral("Yellows"),
      QStringLiteral("Greens"), QStringLiteral("Aquas"), QStringLiteral("Blues"),
      QStringLiteral("Purples"), QStringLiteral("Magentas"),
  };
  // Save current adjustments to the outgoing channel slot.
  const auto old_index = kMixerChannelNames.indexOf(hsl_channel_);
  if (old_index >= 0 && old_index < 8) {
    mixer_channels_[static_cast<std::size_t>(old_index)].hue = hue_adjustment_;
    mixer_channels_[static_cast<std::size_t>(old_index)].saturation = saturation_adjustment_;
    mixer_channels_[static_cast<std::size_t>(old_index)].luminance = luminance_adjustment_;
  }
  hsl_channel_ = normalized;
  // Load adjustments from the incoming channel slot.
  const auto new_index = kMixerChannelNames.indexOf(normalized);
  if (new_index >= 0 && new_index < 8) {
    hue_adjustment_ = mixer_channels_[static_cast<std::size_t>(new_index)].hue;
    saturation_adjustment_ = mixer_channels_[static_cast<std::size_t>(new_index)].saturation;
    luminance_adjustment_ = mixer_channels_[static_cast<std::size_t>(new_index)].luminance;
  } else {
    hue_adjustment_ = 0.0;
    saturation_adjustment_ = 0.0;
    luminance_adjustment_ = 0.0;
  }
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setHueAdjustment(double hue_adjustment) {
  hue_adjustment = clamp_signed_percent(hue_adjustment);
  if (qFuzzyCompare(hue_adjustment_ + 1.0, hue_adjustment + 1.0)) {
    return;
  }
  rememberStateForUndo();
  hue_adjustment_ = hue_adjustment;
  static const QStringList kMixerChannelNames = {
      QStringLiteral("Reds"), QStringLiteral("Oranges"), QStringLiteral("Yellows"),
      QStringLiteral("Greens"), QStringLiteral("Aquas"), QStringLiteral("Blues"),
      QStringLiteral("Purples"), QStringLiteral("Magentas"),
  };
  const auto ch_index = kMixerChannelNames.indexOf(hsl_channel_);
  if (ch_index >= 0 && ch_index < 8) {
    mixer_channels_[static_cast<std::size_t>(ch_index)].hue = hue_adjustment;
  }
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setSaturationAdjustment(double saturation_adjustment) {
  saturation_adjustment = clamp_signed_percent(saturation_adjustment);
  if (qFuzzyCompare(saturation_adjustment_ + 1.0, saturation_adjustment + 1.0)) {
    return;
  }
  rememberStateForUndo();
  saturation_adjustment_ = saturation_adjustment;
  static const QStringList kMixerChannelNames = {
      QStringLiteral("Reds"), QStringLiteral("Oranges"), QStringLiteral("Yellows"),
      QStringLiteral("Greens"), QStringLiteral("Aquas"), QStringLiteral("Blues"),
      QStringLiteral("Purples"), QStringLiteral("Magentas"),
  };
  const auto ch_index = kMixerChannelNames.indexOf(hsl_channel_);
  if (ch_index >= 0 && ch_index < 8) {
    mixer_channels_[static_cast<std::size_t>(ch_index)].saturation = saturation_adjustment;
  }
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setLuminanceAdjustment(double luminance_adjustment) {
  luminance_adjustment = clamp_signed_percent(luminance_adjustment);
  if (qFuzzyCompare(luminance_adjustment_ + 1.0, luminance_adjustment + 1.0)) {
    return;
  }
  rememberStateForUndo();
  luminance_adjustment_ = luminance_adjustment;
  static const QStringList kMixerChannelNames = {
      QStringLiteral("Reds"), QStringLiteral("Oranges"), QStringLiteral("Yellows"),
      QStringLiteral("Greens"), QStringLiteral("Aquas"), QStringLiteral("Blues"),
      QStringLiteral("Purples"), QStringLiteral("Magentas"),
  };
  const auto ch_index = kMixerChannelNames.indexOf(hsl_channel_);
  if (ch_index >= 0 && ch_index < 8) {
    mixer_channels_[static_cast<std::size_t>(ch_index)].luminance = luminance_adjustment;
  }
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setSelectiveCyan(double value) {
  value = clamp_signed_percent(value);
  const auto ch_index = mixer_channel_index(hsl_channel_);
  if (ch_index < 0 || ch_index >= 8) {
    return;
  }
  auto& channel = mixer_channels_[static_cast<std::size_t>(ch_index)];
  if (qFuzzyCompare(channel.selective_cyan + 1.0, value + 1.0)) {
    return;
  }
  rememberStateForUndo();
  channel.selective_cyan = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setSelectiveMagenta(double value) {
  value = clamp_signed_percent(value);
  const auto ch_index = mixer_channel_index(hsl_channel_);
  if (ch_index < 0 || ch_index >= 8) {
    return;
  }
  auto& channel = mixer_channels_[static_cast<std::size_t>(ch_index)];
  if (qFuzzyCompare(channel.selective_magenta + 1.0, value + 1.0)) {
    return;
  }
  rememberStateForUndo();
  channel.selective_magenta = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setSelectiveYellow(double value) {
  value = clamp_signed_percent(value);
  const auto ch_index = mixer_channel_index(hsl_channel_);
  if (ch_index < 0 || ch_index >= 8) {
    return;
  }
  auto& channel = mixer_channels_[static_cast<std::size_t>(ch_index)];
  if (qFuzzyCompare(channel.selective_yellow + 1.0, value + 1.0)) {
    return;
  }
  rememberStateForUndo();
  channel.selective_yellow = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setSelectiveBlack(double value) {
  value = clamp_signed_percent(value);
  const auto ch_index = mixer_channel_index(hsl_channel_);
  if (ch_index < 0 || ch_index >= 8) {
    return;
  }
  auto& channel = mixer_channels_[static_cast<std::size_t>(ch_index)];
  if (qFuzzyCompare(channel.selective_black + 1.0, value + 1.0)) {
    return;
  }
  rememberStateForUndo();
  channel.selective_black = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setChannelMixerRedFromRed(double value) {
  value = clamp_channel_mixer_percent(value);
  if (qFuzzyCompare(rgb_channel_mixer_.red_from_red + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  rgb_channel_mixer_.red_from_red = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setChannelMixerRedFromGreen(double value) {
  value = clamp_channel_mixer_percent(value);
  if (qFuzzyCompare(rgb_channel_mixer_.red_from_green + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  rgb_channel_mixer_.red_from_green = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setChannelMixerRedFromBlue(double value) {
  value = clamp_channel_mixer_percent(value);
  if (qFuzzyCompare(rgb_channel_mixer_.red_from_blue + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  rgb_channel_mixer_.red_from_blue = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setChannelMixerGreenFromRed(double value) {
  value = clamp_channel_mixer_percent(value);
  if (qFuzzyCompare(rgb_channel_mixer_.green_from_red + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  rgb_channel_mixer_.green_from_red = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setChannelMixerGreenFromGreen(double value) {
  value = clamp_channel_mixer_percent(value);
  if (qFuzzyCompare(rgb_channel_mixer_.green_from_green + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  rgb_channel_mixer_.green_from_green = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setChannelMixerGreenFromBlue(double value) {
  value = clamp_channel_mixer_percent(value);
  if (qFuzzyCompare(rgb_channel_mixer_.green_from_blue + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  rgb_channel_mixer_.green_from_blue = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setChannelMixerBlueFromRed(double value) {
  value = clamp_channel_mixer_percent(value);
  if (qFuzzyCompare(rgb_channel_mixer_.blue_from_red + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  rgb_channel_mixer_.blue_from_red = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setChannelMixerBlueFromGreen(double value) {
  value = clamp_channel_mixer_percent(value);
  if (qFuzzyCompare(rgb_channel_mixer_.blue_from_green + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  rgb_channel_mixer_.blue_from_green = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setChannelMixerBlueFromBlue(double value) {
  value = clamp_channel_mixer_percent(value);
  if (qFuzzyCompare(rgb_channel_mixer_.blue_from_blue + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  rgb_channel_mixer_.blue_from_blue = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setBlackAndWhiteEnabled(bool enabled) {
  if (black_and_white_enabled_ == enabled) { return; }
  rememberStateForUndo();
  black_and_white_enabled_ = enabled;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setBlackAndWhiteReds(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(black_and_white_mixer_[0] + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  black_and_white_mixer_[0] = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setBlackAndWhiteOranges(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(black_and_white_mixer_[1] + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  black_and_white_mixer_[1] = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setBlackAndWhiteYellows(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(black_and_white_mixer_[2] + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  black_and_white_mixer_[2] = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setBlackAndWhiteGreens(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(black_and_white_mixer_[3] + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  black_and_white_mixer_[3] = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setBlackAndWhiteAquas(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(black_and_white_mixer_[4] + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  black_and_white_mixer_[4] = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setBlackAndWhiteBlues(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(black_and_white_mixer_[5] + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  black_and_white_mixer_[5] = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setBlackAndWhitePurples(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(black_and_white_mixer_[6] + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  black_and_white_mixer_[6] = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setBlackAndWhiteMagentas(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(black_and_white_mixer_[7] + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  black_and_white_mixer_[7] = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setColorReplacementEnabled(bool enabled) {
  if (color_replacement_enabled_ == enabled) { return; }
  rememberStateForUndo();
  color_replacement_enabled_ = enabled;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setColorReplacementSourceHue(double value) {
  value = value - std::floor(value);
  if (qFuzzyCompare(color_replacement_source_hue_ + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  color_replacement_source_hue_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setColorReplacementTargetHue(double value) {
  value = value - std::floor(value);
  if (qFuzzyCompare(color_replacement_target_hue_ + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  color_replacement_target_hue_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setColorReplacementRange(double value) {
  value = qBound(1.0, value, 100.0);
  if (qFuzzyCompare(color_replacement_range_ + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  color_replacement_range_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setColorReplacementStrength(double value) {
  value = qBound(0.0, value, 100.0);
  if (qFuzzyCompare(color_replacement_strength_ + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  color_replacement_strength_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setSkinToneProtectionEnabled(bool enabled) {
  if (skin_tone_protection_enabled_ == enabled) { return; }
  rememberStateForUndo();
  skin_tone_protection_enabled_ = enabled;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setSkinToneLineHue(double value) {
  value = value - std::floor(value);
  if (qFuzzyCompare(skin_tone_line_hue_ + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  skin_tone_line_hue_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setSkinToneProtectionRange(double value) {
  value = qBound(1.0, value, 100.0);
  if (qFuzzyCompare(skin_tone_protection_range_ + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  skin_tone_protection_range_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setSkinToneProtectionStrength(double value) {
  value = qBound(0.0, value, 100.0);
  if (qFuzzyCompare(skin_tone_protection_strength_ + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  skin_tone_protection_strength_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setColorHarmonyEnabled(bool enabled) {
  if (color_harmony_enabled_ == enabled) { return; }
  rememberStateForUndo();
  color_harmony_enabled_ = enabled;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setColorHarmonyMode(const QString& mode) {
  const auto normalized =
      (mode == QStringLiteral("Analogous") || mode == QStringLiteral("Triadic") ||
       mode == QStringLiteral("Split Complementary") || mode == QStringLiteral("Tetradic"))
          ? mode
          : QStringLiteral("Complementary");
  if (color_harmony_mode_ == normalized) { return; }
  rememberStateForUndo();
  color_harmony_mode_ = normalized;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setColorHarmonyBaseHue(double value) {
  value = value - std::floor(value);
  if (qFuzzyCompare(color_harmony_base_hue_ + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  color_harmony_base_hue_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setColorHarmonyStrength(double value) {
  value = qBound(0.0, value, 100.0);
  if (qFuzzyCompare(color_harmony_strength_ + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  color_harmony_strength_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setGradingShadowsHue(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(grading_shadows_hue_ + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  grading_shadows_hue_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setGradingShadowsSaturation(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(grading_shadows_saturation_ + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  grading_shadows_saturation_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setGradingShadowsLuminance(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(grading_shadows_luminance_ + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  grading_shadows_luminance_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setGradingMidtonesHue(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(grading_midtones_hue_ + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  grading_midtones_hue_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setGradingMidtonesSaturation(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(grading_midtones_saturation_ + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  grading_midtones_saturation_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setGradingMidtonesLuminance(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(grading_midtones_luminance_ + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  grading_midtones_luminance_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setGradingHighlightsHue(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(grading_highlights_hue_ + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  grading_highlights_hue_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setGradingHighlightsSaturation(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(grading_highlights_saturation_ + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  grading_highlights_saturation_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setGradingHighlightsLuminance(double value) {
  value = clamp_signed_percent(value);
  if (qFuzzyCompare(grading_highlights_luminance_ + 1.0, value + 1.0)) { return; }
  rememberStateForUndo();
  grading_highlights_luminance_ = value;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::updateCropRect(double x, double y, double width, double height) {
  const auto normalized_width = qBound(0.05, width, 1.0);
  const auto normalized_height = qBound(0.05, height, 1.0);
  const auto normalized_x = qBound(0.0, x, 1.0 - normalized_width);
  const auto normalized_y = qBound(0.0, y, 1.0 - normalized_height);

  if (qFuzzyCompare(crop_rect_x_ + 1.0, normalized_x + 1.0) &&
      qFuzzyCompare(crop_rect_y_ + 1.0, normalized_y + 1.0) &&
      qFuzzyCompare(crop_rect_width_ + 1.0, normalized_width + 1.0) &&
      qFuzzyCompare(crop_rect_height_ + 1.0, normalized_height + 1.0)) {
    return;
  }

  rememberStateForUndo();
  crop_rect_x_ = normalized_x;
  crop_rect_y_ = normalized_y;
  crop_rect_width_ = normalized_width;
  crop_rect_height_ = normalized_height;
  persistCurrentState();
  emit controlsChanged();
  if (!crop_editing_) {
    renderPreview(false, QStringLiteral("crop_change"));
  }
}

void EditorPreviewController::resetCropRect() {
  if (qFuzzyIsNull(crop_rect_width_) && qFuzzyIsNull(crop_rect_height_)) {
    return;
  }

  rememberStateForUndo();
  crop_rect_x_ = 0.0;
  crop_rect_y_ = 0.0;
  crop_rect_width_ = 0.0;
  crop_rect_height_ = 0.0;
  persistCurrentState();
  emit controlsChanged();
  if (!crop_editing_) {
    renderPreview(false, QStringLiteral("crop_change"));
  }
}

bool EditorPreviewController::resetTransform() {
  if (qFuzzyIsNull(perspective_vertical_) &&
      qFuzzyIsNull(perspective_horizontal_) &&
      qFuzzyIsNull(transform_rotation_) &&
      qFuzzyCompare(transform_scale_, 1.0) &&
      qFuzzyIsNull(transform_skew_x_) &&
      qFuzzyIsNull(transform_skew_y_) &&
      qFuzzyCompare(liquify_center_x_, 0.5) &&
      qFuzzyCompare(liquify_center_y_, 0.5) &&
      qFuzzyCompare(liquify_radius_, 0.25) &&
      qFuzzyIsNull(liquify_strength_) &&
      !content_aware_crop_fill_ &&
      upright_mode_ == QStringLiteral("Off") &&
      qFuzzyIsNull(upright_vertical_guide_angle_) &&
      qFuzzyIsNull(upright_horizontal_guide_angle_)) {
    return false;
  }

  rememberStateForUndo();
  perspective_vertical_ = 0.0;
  perspective_horizontal_ = 0.0;
  transform_rotation_ = 0.0;
  transform_scale_ = 1.0;
  transform_skew_x_ = 0.0;
  transform_skew_y_ = 0.0;
  liquify_center_x_ = 0.5;
  liquify_center_y_ = 0.5;
  liquify_radius_ = 0.25;
  liquify_strength_ = 0.0;
  content_aware_crop_fill_ = false;
  upright_mode_ = QStringLiteral("Off");
  upright_vertical_guide_angle_ = 0.0;
  upright_horizontal_guide_angle_ = 0.0;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
  return true;
}

void EditorPreviewController::setCropZoom(double crop_zoom) {
  crop_zoom = qBound(1.0, crop_zoom, 3.0);
  if (qFuzzyCompare(crop_zoom_, crop_zoom)) {
    return;
  }
  rememberStateForUndo();
  const auto previous_zoom = crop_zoom_;
  crop_zoom_ = crop_zoom;
  const auto size = sourceImageSize();
  if (size.isValid()) {
    const auto current_rect = normalized_crop_rect_for_state(size, currentState());
    const auto center_x = current_rect.x() + current_rect.width() * 0.5;
    const auto center_y = current_rect.y() + current_rect.height() * 0.5;
    const auto scale = previous_zoom / crop_zoom_;
    const auto width = qBound(0.05, current_rect.width() * scale, 1.0);
    const auto height = qBound(0.05, current_rect.height() * scale, 1.0);
    crop_rect_width_ = width;
    crop_rect_height_ = height;
    crop_rect_x_ = qBound(0.0, center_x - width * 0.5, 1.0 - width);
    crop_rect_y_ = qBound(0.0, center_y - height * 0.5, 1.0 - height);
  }
  persistCurrentState();
  emit controlsChanged();
  if (!crop_editing_) {
    renderPreview(false, QStringLiteral("crop_change"));
  }
}

void EditorPreviewController::setCropPreset(const QString& crop_preset) {
  const auto normalized =
      crop_preset.isEmpty() ? QStringLiteral("Original") : crop_preset;
  if (crop_preset_ == normalized) {
    return;
  }
  rememberStateForUndo();
  crop_preset_ = normalized;
  const auto size = sourceImageSize();
  if (size.isValid()) {
    auto rect = normalized_crop_rect_for_state(size, currentState());
    const auto aspect_ratio = crop_aspect_ratio(crop_preset_);
    if (aspect_ratio > 0.0) {
      const auto center_x = rect.x() + rect.width() * 0.5;
      const auto center_y = rect.y() + rect.height() * 0.5;
      auto width = rect.width();
      auto height = rect.height();
      const auto current_ratio = width / std::max(1e-6, height);
      if (current_ratio > aspect_ratio) {
        width = height * aspect_ratio;
      } else {
        height = width / aspect_ratio;
      }
      width = qBound(0.05, width, 1.0);
      height = qBound(0.05, height, 1.0);
      crop_rect_width_ = width;
      crop_rect_height_ = height;
      crop_rect_x_ = qBound(0.0, center_x - width * 0.5, 1.0 - width);
      crop_rect_y_ = qBound(0.0, center_y - height * 0.5, 1.0 - height);
    } else {
      crop_rect_x_ = rect.x();
      crop_rect_y_ = rect.y();
      crop_rect_width_ = rect.width();
      crop_rect_height_ = rect.height();
    }
  }
  persistCurrentState();
  emit controlsChanged();
  if (!crop_editing_) {
    renderPreview(false, QStringLiteral("crop_change"));
  }
}

void EditorPreviewController::applyGuidedUpright() {
  const auto next_vertical = guided_upright_perspective(upright_vertical_guide_angle_);
  const auto next_horizontal = guided_upright_perspective(upright_horizontal_guide_angle_);
  if (upright_mode_ == QStringLiteral("Guided") &&
      qFuzzyCompare(perspective_vertical_ + 1.0, next_vertical + 1.0) &&
      qFuzzyCompare(perspective_horizontal_ + 1.0, next_horizontal + 1.0)) {
    return;
  }

  rememberStateForUndo();
  upright_mode_ = QStringLiteral("Guided");
  perspective_vertical_ = next_vertical;
  perspective_horizontal_ = next_horizontal;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

bool EditorPreviewController::straightenFromHorizon(
    double start_x,
    double start_y,
    double end_x,
    double end_y) {
  const auto angle = horizon_angle_degrees(start_x, start_y, end_x, end_y);
  if (!std::isfinite(angle)) {
    return false;
  }
  return straightenFromHorizonAngle(angle);
}

bool EditorPreviewController::straightenFromHorizonAngle(double horizon_angle) {
  if (!std::isfinite(horizon_angle)) {
    return false;
  }
  horizon_angle = qBound(-45.0, horizon_angle, 45.0);
  const auto next_rotation = qBound(-45.0, transform_rotation_ - horizon_angle, 45.0);
  if (qFuzzyCompare(transform_rotation_ + 1.0, next_rotation + 1.0) &&
      qFuzzyCompare(upright_horizontal_guide_angle_ + 1.0, horizon_angle + 1.0)) {
    return false;
  }

  rememberStateForUndo();
  upright_horizontal_guide_angle_ = horizon_angle;
  transform_rotation_ = next_rotation;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
  return true;
}

void EditorPreviewController::setUprightMode(const QString& upright_mode) {
  const auto normalized = normalized_upright_mode_name(upright_mode);
  if (upright_mode_ == normalized) {
    return;
  }
  rememberStateForUndo();
  upright_mode_ = normalized;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setUprightVerticalGuideAngle(double guide_angle) {
  guide_angle = qBound(-45.0, guide_angle, 45.0);
  const auto next_perspective =
      upright_mode_ == QStringLiteral("Guided") ? guided_upright_perspective(guide_angle) : perspective_vertical_;
  if (qFuzzyCompare(upright_vertical_guide_angle_ + 1.0, guide_angle + 1.0) &&
      qFuzzyCompare(perspective_vertical_ + 1.0, next_perspective + 1.0)) {
    return;
  }

  rememberStateForUndo();
  upright_vertical_guide_angle_ = guide_angle;
  perspective_vertical_ = next_perspective;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setUprightHorizontalGuideAngle(double guide_angle) {
  guide_angle = qBound(-45.0, guide_angle, 45.0);
  const auto next_perspective =
      upright_mode_ == QStringLiteral("Guided") ? guided_upright_perspective(guide_angle) : perspective_horizontal_;
  if (qFuzzyCompare(upright_horizontal_guide_angle_ + 1.0, guide_angle + 1.0) &&
      qFuzzyCompare(perspective_horizontal_ + 1.0, next_perspective + 1.0)) {
    return;
  }

  rememberStateForUndo();
  upright_horizontal_guide_angle_ = guide_angle;
  perspective_horizontal_ = next_perspective;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setActiveMaskIndex(int index) {
  const auto normalized_index =
      mask_stack_.isEmpty() || index < 0
          ? -1
          : qBound(0, index, static_cast<int>(mask_stack_.size()) - 1);
  if (active_mask_index_ == normalized_index) {
    return;
  }
  active_mask_index_ = normalized_index;
  invalidateActiveMaskCache();
  emit controlsChanged();
  emit maskOverlayChanged();
}

void EditorPreviewController::setActiveMaskName(const QString& name) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  const auto normalized_name = name.simplified();
  mask.insert(QStringLiteral("name"), normalized_name);
  if (is_selection_mask(mask)) {
    mask.insert(QStringLiteral("maskSetName"), normalized_name);
  }
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setInpaintPrompt(const QString& prompt) {
  const auto normalized_prompt = prompt.simplified();
  if (inpaint_prompt_ == normalized_prompt) {
    return;
  }
  inpaint_prompt_ = normalized_prompt;
  emit controlsChanged();
}

void EditorPreviewController::setInpaintSeed(int seed) {
  const auto normalized_seed = std::max(1, seed);
  if (inpaint_seed_ == normalized_seed) {
    return;
  }
  inpaint_seed_ = normalized_seed;
  emit controlsChanged();
}

void EditorPreviewController::setInpaintBlendAmount(double amount) {
  const auto normalized_amount = qBound(0.0, amount, 1.0);
  if (std::abs(inpaint_blend_amount_ - normalized_amount) <= 1e-6) {
    return;
  }
  inpaint_blend_amount_ = normalized_amount;
  emit controlsChanged();
}

void EditorPreviewController::setInpaintQualityMode(const QString& mode) {
  const auto normalized_mode =
      mode.trimmed().toLower() == QStringLiteral("final") ? QStringLiteral("final")
                                                          : QStringLiteral("preview");
  if (inpaint_quality_mode_ == normalized_mode) {
    return;
  }
  inpaint_quality_mode_ = normalized_mode;
  emit controlsChanged();
}

void EditorPreviewController::setActiveMaskOperation(const QString& operation) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("operation"), normalized_mask_operation(operation));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskEnabled(bool enabled) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("enabled"), enabled);
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskSolo(bool solo) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("solo"), solo);
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskLocked(bool locked) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("locked"), locked);
  updateActiveMaskDefinition(mask, true, true);
}

void EditorPreviewController::setActiveMaskOpacity(double opacity) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("opacity"), qBound(0.0, opacity, 1.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskDensity(double density) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("density"), qBound(0.0, density, 1.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskInverted(bool inverted) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("invert"), inverted);
  mask.insert(QStringLiteral("selectionInvert"), inverted);
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskBlendMode(const QString& blend_mode) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("blendMode"), normalized_mask_blend_mode(blend_mode));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskExposure(double local_exposure) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("exposure"), qBound(-2.0, local_exposure, 2.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskContrast(double contrast) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("contrast"), clamp_signed_percent(contrast));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskHighlights(double highlights) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("highlights"), clamp_signed_percent(highlights));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskShadows(double shadows) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("shadows"), clamp_signed_percent(shadows));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskWhites(double whites) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("whites"), clamp_signed_percent(whites));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskBlacks(double blacks) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("blacks"), clamp_signed_percent(blacks));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskTemperature(double local_temperature) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("temperature"), clamp_signed_percent(local_temperature));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskTint(double tint) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("tint"), clamp_signed_percent(tint));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskVibrance(double vibrance) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("vibrance"), clamp_signed_percent(vibrance));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskSaturation(double saturation) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("saturation"), clamp_signed_percent(saturation));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskHueShift(double hue_shift) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("hueShift"), clamp_signed_percent(hue_shift));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskSaturationShift(double saturation_shift) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("saturationShift"), clamp_signed_percent(saturation_shift));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskColorizeHue(double colorize_hue) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  auto wrapped = colorize_hue - std::floor(colorize_hue);
  if (wrapped < 0.0) {
    wrapped += 1.0;
  }
  mask.insert(QStringLiteral("colorizeHue"), wrapped);
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskColorizeStrength(double colorize_strength) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("colorizeStrength"), qBound(0.0, colorize_strength, 100.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskRemoveStrength(double remove_strength) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("removeStrength"), qBound(0.0, remove_strength, 100.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskSelectionInverted(bool selection_inverted) {
  setActiveMaskInverted(selection_inverted);
}

void EditorPreviewController::setActiveMaskSelectionExpand(double selection_expand) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("selectionExpand"), clamp_signed_percent(selection_expand));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskSelectionSmooth(double selection_smooth) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("selectionSmooth"), qBound(0.0, selection_smooth, 100.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskSelectionFineEdge(double fine_edge) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("selectionFineEdge"), qBound(0.0, fine_edge, 100.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskSelectionEdgeContrast(double edge_contrast) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("selectionEdgeContrast"), qBound(0.0, edge_contrast, 100.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskSelectionDecontaminate(double decontaminate) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("selectionDecontaminate"), qBound(0.0, decontaminate, 100.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskFeather(double local_feather) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  const auto minimum =
      activeMaskKind() == QStringLiteral("polygon") ? 0.0 : 0.05;
  const auto maximum =
      activeMaskKind() == QStringLiteral("polygon") ? 0.2 : 1.0;
  mask.insert(QStringLiteral("feather"), qBound(minimum, local_feather, maximum));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskPosition(double local_position) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("linear")) {
    return;
  }
  mask.insert(QStringLiteral("position"), qBound(0.0, local_position, 1.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskRotation(double local_rotation) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("rotation"), qBound(-180.0, local_rotation, 180.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskCenterX(double center_x) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("radial")) {
    return;
  }
  mask.insert(QStringLiteral("centerX"), qBound(0.0, center_x, 1.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskCenterY(double center_y) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("radial")) {
    return;
  }
  mask.insert(QStringLiteral("centerY"), qBound(0.0, center_y, 1.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskRadiusX(double radius_x) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("radial")) {
    return;
  }
  mask.insert(QStringLiteral("radiusX"), qBound(0.05, radius_x, 1.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskRadiusY(double radius_y) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("radial")) {
    return;
  }
  mask.insert(QStringLiteral("radiusY"), qBound(0.05, radius_y, 1.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskBrushSize(double brush_size) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("brush")) {
    return;
  }
  mask.insert(QStringLiteral("brushPreset"), QStringLiteral("Custom"));
  mask.insert(QStringLiteral("brushSize"), qBound(0.01, brush_size, 0.35));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskBrushFlow(double brush_flow) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("brush")) {
    return;
  }
  mask.insert(QStringLiteral("brushPreset"), QStringLiteral("Custom"));
  mask.insert(QStringLiteral("brushFlow"), qBound(0.05, brush_flow, 1.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskBrushHardness(double brush_hardness) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("brush")) {
    return;
  }
  mask.insert(QStringLiteral("brushPreset"), QStringLiteral("Custom"));
  mask.insert(QStringLiteral("brushHardness"), qBound(0.0, brush_hardness, 1.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskBrushSpacing(double brush_spacing) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("brush")) {
    return;
  }
  mask.insert(QStringLiteral("brushPreset"), QStringLiteral("Custom"));
  mask.insert(QStringLiteral("brushSpacing"), qBound(0.05, brush_spacing, 2.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskBrushSmoothing(double brush_smoothing) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("brush")) {
    return;
  }
  mask.insert(QStringLiteral("brushPreset"), QStringLiteral("Custom"));
  mask.insert(QStringLiteral("brushSmoothing"), qBound(0.0, brush_smoothing, 1.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskBrushBuildup(bool brush_buildup) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("brush")) {
    return;
  }
  mask.insert(QStringLiteral("brushPreset"), QStringLiteral("Custom"));
  mask.insert(QStringLiteral("brushBuildup"), brush_buildup);
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskBrushEraseMode(bool erase_mode) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("brush")) {
    return;
  }
  mask.insert(QStringLiteral("brushPreset"), QStringLiteral("Custom"));
  mask.insert(QStringLiteral("brushEraseMode"), erase_mode);
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskBrushAutoMask(bool auto_mask) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("brush")) {
    return;
  }
  mask.insert(QStringLiteral("brushPreset"), QStringLiteral("Custom"));
  mask.insert(QStringLiteral("brushAutoMask"), auto_mask);
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskBrushAutoMaskTolerance(double tolerance) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("brush")) {
    return;
  }
  mask.insert(QStringLiteral("brushPreset"), QStringLiteral("Custom"));
  mask.insert(QStringLiteral("brushAutoMaskTolerance"), qBound(0.02, tolerance, 0.75));
  updateActiveMaskDefinition(mask);
}

bool EditorPreviewController::applyActiveMaskBrushPreset(const QString& name) {
  const auto* preset = brush_preset_by_name(name);
  auto mask = activeMaskDefinition();
  if (preset == nullptr || mask.isEmpty() || activeMaskKind() != QStringLiteral("brush")) {
    return false;
  }

  apply_brush_preset_to_mask(&mask, *preset);
  updateActiveMaskDefinition(mask);
  setStatus(QStringLiteral("Applied brush preset '%1'.").arg(preset->name));
  return true;
}

void EditorPreviewController::setActiveMaskCloneScale(double clone_scale) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("brush") ||
      activeMaskRetouchMode() != QStringLiteral("clone")) {
    return;
  }
  mask.insert(QStringLiteral("cloneScale"), qBound(0.25, clone_scale, 4.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskCloneRotation(double clone_rotation) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("brush") ||
      activeMaskRetouchMode() != QStringLiteral("clone")) {
    return;
  }
  mask.insert(QStringLiteral("cloneRotation"), qBound(-180.0, clone_rotation, 180.0));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskLuminanceMin(double min_value) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("luminance")) {
    return;
  }
  mask.insert(QStringLiteral("luminanceMin"), qBound(0.0, min_value, 1.0));
  if (mask.value(QStringLiteral("luminanceMin")).toDouble() > mask.value(QStringLiteral("luminanceMax")).toDouble()) {
    mask.insert(QStringLiteral("luminanceMax"), mask.value(QStringLiteral("luminanceMin")).toDouble());
  }
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskLuminanceMax(double max_value) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("luminance")) {
    return;
  }
  mask.insert(QStringLiteral("luminanceMax"), qBound(0.0, max_value, 1.0));
  if (mask.value(QStringLiteral("luminanceMax")).toDouble() < mask.value(QStringLiteral("luminanceMin")).toDouble()) {
    mask.insert(QStringLiteral("luminanceMin"), mask.value(QStringLiteral("luminanceMax")).toDouble());
  }
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskHueCenter(double center) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("color")) {
    return;
  }
  auto wrapped = center - std::floor(center);
  if (wrapped < 0.0) {
    wrapped += 1.0;
  }
  mask.insert(QStringLiteral("hueCenter"), wrapped);
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskHueRange(double range) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("color")) {
    return;
  }
  mask.insert(QStringLiteral("hueRange"), qBound(0.02, range, 0.5));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskSaturationMin(double min_value) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("color")) {
    return;
  }
  mask.insert(QStringLiteral("saturationMin"), qBound(0.0, min_value, 1.0));
  if (mask.value(QStringLiteral("saturationMin")).toDouble() > mask.value(QStringLiteral("saturationMax")).toDouble()) {
    mask.insert(QStringLiteral("saturationMax"), mask.value(QStringLiteral("saturationMin")).toDouble());
  }
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskSaturationMax(double max_value) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("color")) {
    return;
  }
  mask.insert(QStringLiteral("saturationMax"), qBound(0.0, max_value, 1.0));
  if (mask.value(QStringLiteral("saturationMax")).toDouble() < mask.value(QStringLiteral("saturationMin")).toDouble()) {
    mask.insert(QStringLiteral("saturationMin"), mask.value(QStringLiteral("saturationMax")).toDouble());
  }
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskDepthMin(double min_value) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("depth")) {
    return;
  }
  mask.insert(QStringLiteral("depthMin"), qBound(0.0, min_value, 1.0));
  if (mask.value(QStringLiteral("depthMin")).toDouble() > mask.value(QStringLiteral("depthMax")).toDouble()) {
    mask.insert(QStringLiteral("depthMax"), mask.value(QStringLiteral("depthMin")).toDouble());
  }
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskDepthMax(double max_value) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("depth")) {
    return;
  }
  mask.insert(QStringLiteral("depthMax"), qBound(0.0, max_value, 1.0));
  if (mask.value(QStringLiteral("depthMax")).toDouble() < mask.value(QStringLiteral("depthMin")).toDouble()) {
    mask.insert(QStringLiteral("depthMin"), mask.value(QStringLiteral("depthMax")).toDouble());
  }
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskRangeFeather(double feather) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() ||
      (activeMaskKind() != QStringLiteral("luminance") && activeMaskKind() != QStringLiteral("color") &&
       activeMaskKind() != QStringLiteral("depth"))) {
    return;
  }
  mask.insert(QStringLiteral("rangeFeather"), qBound(0.0, feather, 0.5));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskRefineField(
    const QString& key,
    double value,
    double minimum,
    double maximum) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskToolRole() == QStringLiteral("retouch")) {
    return;
  }
  mask.insert(key, qBound(minimum, value, maximum));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskRefineMinMaxField(
    const QString& key,
    const QString& paired_key,
    double value,
    bool is_minimum) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskToolRole() == QStringLiteral("retouch")) {
    return;
  }
  const auto bounded = qBound(0.0, value, 1.0);
  mask.insert(key, bounded);
  const auto paired_value = map_double_value(mask, paired_key, is_minimum ? 1.0 : 0.0);
  if (is_minimum && bounded > paired_value) {
    mask.insert(paired_key, bounded);
  } else if (!is_minimum && bounded < paired_value) {
    mask.insert(paired_key, bounded);
  }
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskRefineLuminanceMin(double min_value) {
  setActiveMaskRefineMinMaxField(
      QStringLiteral("refineLuminanceMin"),
      QStringLiteral("refineLuminanceMax"),
      min_value,
      true);
}

void EditorPreviewController::setActiveMaskRefineLuminanceMax(double max_value) {
  setActiveMaskRefineMinMaxField(
      QStringLiteral("refineLuminanceMax"),
      QStringLiteral("refineLuminanceMin"),
      max_value,
      false);
}

void EditorPreviewController::setActiveMaskRefineHueCenter(double center) {
  auto wrapped = center - std::floor(center);
  if (wrapped < 0.0) {
    wrapped += 1.0;
  }
  setActiveMaskRefineField(QStringLiteral("refineHueCenter"), wrapped, 0.0, 1.0);
}

void EditorPreviewController::setActiveMaskRefineHueRange(double range) {
  setActiveMaskRefineField(QStringLiteral("refineHueRange"), range, 0.02, 0.5);
}

void EditorPreviewController::setActiveMaskRefineSaturationMin(double min_value) {
  setActiveMaskRefineMinMaxField(
      QStringLiteral("refineSaturationMin"),
      QStringLiteral("refineSaturationMax"),
      min_value,
      true);
}

void EditorPreviewController::setActiveMaskRefineSaturationMax(double max_value) {
  setActiveMaskRefineMinMaxField(
      QStringLiteral("refineSaturationMax"),
      QStringLiteral("refineSaturationMin"),
      max_value,
      false);
}

void EditorPreviewController::setActiveMaskRefineDepthMin(double min_value) {
  setActiveMaskRefineMinMaxField(
      QStringLiteral("refineDepthMin"),
      QStringLiteral("refineDepthMax"),
      min_value,
      true);
}

void EditorPreviewController::setActiveMaskRefineDepthMax(double max_value) {
  setActiveMaskRefineMinMaxField(
      QStringLiteral("refineDepthMax"),
      QStringLiteral("refineDepthMin"),
      max_value,
      false);
}

void EditorPreviewController::setActiveMaskRefineEdgeConfidenceMin(double min_value) {
  setActiveMaskRefineField(QStringLiteral("refineEdgeConfidenceMin"), min_value, 0.0, 1.0);
}

void EditorPreviewController::setActiveMaskRefineRangeFeather(double feather) {
  setActiveMaskRefineField(QStringLiteral("refineRangeFeather"), feather, 0.0, 0.5);
}

void EditorPreviewController::setLinearMaskEnabled(bool enabled) {
  QVariantMap updates;
  updates.insert(QStringLiteral("enabled"), enabled);
  updateLegacyKindMask(QStringLiteral("linear"), updates);
}

void EditorPreviewController::setLinearMaskExposure(double local_exposure) {
  QVariantMap updates;
  updates.insert(QStringLiteral("exposure"), qBound(-2.0, local_exposure, 2.0));
  updateLegacyKindMask(QStringLiteral("linear"), updates);
}

void EditorPreviewController::setLinearMaskTemperature(double local_temperature) {
  QVariantMap updates;
  updates.insert(QStringLiteral("temperature"), clamp_signed_percent(local_temperature));
  updateLegacyKindMask(QStringLiteral("linear"), updates);
}

void EditorPreviewController::setLinearMaskFeather(double local_feather) {
  QVariantMap updates;
  updates.insert(QStringLiteral("feather"), qBound(0.05, local_feather, 1.0));
  updateLegacyKindMask(QStringLiteral("linear"), updates);
}

void EditorPreviewController::setLinearMaskPosition(double local_position) {
  QVariantMap updates;
  updates.insert(QStringLiteral("position"), qBound(0.0, local_position, 1.0));
  updateLegacyKindMask(QStringLiteral("linear"), updates);
}

void EditorPreviewController::setLinearMaskRotation(double local_rotation) {
  QVariantMap updates;
  updates.insert(QStringLiteral("rotation"), qBound(-180.0, local_rotation, 180.0));
  updateLegacyKindMask(QStringLiteral("linear"), updates);
}

void EditorPreviewController::setRadialMaskEnabled(bool enabled) {
  QVariantMap updates;
  updates.insert(QStringLiteral("enabled"), enabled);
  updateLegacyKindMask(QStringLiteral("radial"), updates);
}

void EditorPreviewController::setRadialMaskExposure(double local_exposure) {
  QVariantMap updates;
  updates.insert(QStringLiteral("exposure"), qBound(-2.0, local_exposure, 2.0));
  updateLegacyKindMask(QStringLiteral("radial"), updates);
}

void EditorPreviewController::setRadialMaskTemperature(double local_temperature) {
  QVariantMap updates;
  updates.insert(QStringLiteral("temperature"), clamp_signed_percent(local_temperature));
  updateLegacyKindMask(QStringLiteral("radial"), updates);
}

void EditorPreviewController::setRadialMaskFeather(double local_feather) {
  QVariantMap updates;
  updates.insert(QStringLiteral("feather"), qBound(0.05, local_feather, 1.0));
  updateLegacyKindMask(QStringLiteral("radial"), updates);
}

void EditorPreviewController::setRadialMaskCenterX(double center_x) {
  QVariantMap updates;
  updates.insert(QStringLiteral("centerX"), qBound(0.0, center_x, 1.0));
  updateLegacyKindMask(QStringLiteral("radial"), updates);
}

void EditorPreviewController::setRadialMaskCenterY(double center_y) {
  QVariantMap updates;
  updates.insert(QStringLiteral("centerY"), qBound(0.0, center_y, 1.0));
  updateLegacyKindMask(QStringLiteral("radial"), updates);
}

void EditorPreviewController::setRadialMaskRadiusX(double radius_x) {
  QVariantMap updates;
  updates.insert(QStringLiteral("radiusX"), qBound(0.05, radius_x, 1.0));
  updateLegacyKindMask(QStringLiteral("radial"), updates);
}

void EditorPreviewController::setRadialMaskRadiusY(double radius_y) {
  QVariantMap updates;
  updates.insert(QStringLiteral("radiusY"), qBound(0.05, radius_y, 1.0));
  updateLegacyKindMask(QStringLiteral("radial"), updates);
}

void EditorPreviewController::setRadialMaskRotation(double local_rotation) {
  QVariantMap updates;
  updates.insert(QStringLiteral("rotation"), qBound(-180.0, local_rotation, 180.0));
  updateLegacyKindMask(QStringLiteral("radial"), updates);
}

void EditorPreviewController::loadAsset(
    const QString& asset_id,
    const QString& file_path,
    const QString& thumbnail_source,
    const QString& camera_label,
    const QString& capture_date,
    const QString& lens_model,
    const QString& recipe_json) {
  persistCurrentState();
  asset_id_ = asset_id;
  file_path_ = file_path;
  fallback_thumbnail_source_ = thumbnail_source;
  camera_label_ = camera_label;
  capture_date_ = capture_date;
  lens_model_ = lens_model;
  brush_stroke_active_ = false;
  brush_stroke_suspends_side_effects_ = false;
  brush_stroke_needs_final_render_ = false;
  active_brush_live_points_.clear();
  clearActiveBrushLivePreview();
  resetSmartSelectInstanceMap();
  restoreStateForAsset(asset_id_, recipe_json);
  refreshRawAsShotWhiteBalanceMetadata();
  setWhiteBalanceSampling(false);
  observability::logInfo(
      photoEditorPreviewLog(),
      QStringLiteral("preview.asset_loaded"),
      {
          {QStringLiteral("asset_id"), asset_id_},
          {QStringLiteral("file_path"), file_path_},
          {QStringLiteral("thumbnail"), fallback_thumbnail_source_},
      });
  refreshDebugInfo();
  emit cameraDefaultsChanged();
  emit assetChanged();
  renderPreview(true, QStringLiteral("asset_select"));
}

void EditorPreviewController::undo() {
  if (!canUndo()) {
    return;
  }

  auto& undo_stack = undoStackForAsset(asset_id_);
  const auto previous_state = undo_stack.takeLast();
  auto& redo_stack = redoStackForAsset(asset_id_);
  const auto current_state = currentState();
  if (redo_stack.isEmpty() || !edit_states_equal(redo_stack.back(), current_state)) {
    redo_stack.push_back(current_state);
    if (redo_stack.size() > kMaxEditHistoryDepth) {
      redo_stack.removeFirst();
    }
  }
  applyHistoryState(previous_state);
}

void EditorPreviewController::redo() {
  if (!canRedo()) {
    return;
  }

  auto& redo_stack = redoStackForAsset(asset_id_);
  const auto next_state = redo_stack.takeLast();
  pushUndoState(currentState());
  applyHistoryState(next_state);
}

void EditorPreviewController::resetAdjustments() {
  if (edit_states_equal(currentState(), EditState{})) {
    return;
  }
  rememberStateForUndo();
  exposure_ = 0.0;
  contrast_ = 0.0;
  highlights_ = 0.0;
  shadows_ = 0.0;
  whites_ = 0.0;
  blacks_ = 0.0;
  brightness_ = 0.0;
  hdr_local_tone_mapping_ = 0.0;
  filmic_shoulder_ = 0.0;
  filmic_toe_ = 0.0;
  adaptive_contrast_ = 0.0;
  levels_black_point_ = 0.0;
  levels_gamma_ = 0.0;
  levels_white_point_ = 0.0;
  zone_deep_shadows_ = 0.0;
  zone_shadows_ = 0.0;
  zone_midtones_ = 0.0;
  zone_lights_ = 0.0;
  zone_highlights_ = 0.0;
  temperature_ = 0.0;
  tint_ = 0.0;
  texture_ = 0.0;
  clarity_ = 0.0;
  dehaze_ = 0.0;
  sharpening_ = 0.0;
  sharpen_masking_ = 0.0;
  luminance_nr_ = 0.0;
  color_nr_ = 0.0;
  lens_distortion_ = 0.0;
  lens_vignette_ = 0.0;
  chromatic_aberration_ = 0.0;
  perspective_vertical_ = 0.0;
  perspective_horizontal_ = 0.0;
  transform_rotation_ = 0.0;
  transform_scale_ = 1.0;
  transform_skew_x_ = 0.0;
  transform_skew_y_ = 0.0;
  liquify_center_x_ = 0.5;
  liquify_center_y_ = 0.5;
  liquify_radius_ = 0.25;
  liquify_strength_ = 0.0;
  content_aware_crop_fill_ = false;
  vibrance_ = 0.0;
  saturation_ = 0.0;
  curve_shadows_ = 0.0;
  curve_darks_ = 0.0;
  curve_lights_ = 0.0;
  curve_highlights_ = 0.0;
  curve_channel_ = QStringLiteral("RGB");
  master_curve_points_.clear();
  luma_curve_points_.clear();
  red_curve_points_.clear();
  green_curve_points_.clear();
  blue_curve_points_.clear();
  color_mixer_model_ = QStringLiteral("OKLab");
  hsl_channel_ = QStringLiteral("Reds");
  hue_adjustment_ = 0.0;
  saturation_adjustment_ = 0.0;
  luminance_adjustment_ = 0.0;
  mixer_channels_ = {};
  rgb_channel_mixer_ = {};
  black_and_white_enabled_ = false;
  black_and_white_mixer_ = {};
  color_replacement_enabled_ = false;
  color_replacement_source_hue_ = 0.0;
  color_replacement_target_hue_ = 0.33;
  color_replacement_range_ = 35.0;
  color_replacement_strength_ = 0.0;
  skin_tone_protection_enabled_ = false;
  skin_tone_line_hue_ = 0.08;
  skin_tone_protection_range_ = 30.0;
  skin_tone_protection_strength_ = 0.0;
  color_harmony_enabled_ = false;
  color_harmony_mode_ = QStringLiteral("Complementary");
  color_harmony_base_hue_ = 0.0;
  color_harmony_strength_ = 0.0;
  grading_shadows_hue_ = 0.0;
  grading_shadows_saturation_ = 0.0;
  grading_shadows_luminance_ = 0.0;
  grading_midtones_hue_ = 0.0;
  grading_midtones_saturation_ = 0.0;
  grading_midtones_luminance_ = 0.0;
  grading_highlights_hue_ = 0.0;
  grading_highlights_saturation_ = 0.0;
  grading_highlights_luminance_ = 0.0;
  crop_rect_x_ = 0.0;
  crop_rect_y_ = 0.0;
  crop_rect_width_ = 0.0;
  crop_rect_height_ = 0.0;
  crop_zoom_ = 1.0;
  crop_preset_ = QStringLiteral("Original");
  upright_mode_ = QStringLiteral("Off");
  upright_vertical_guide_angle_ = 0.0;
  upright_horizontal_guide_angle_ = 0.0;
  input_color_profile_ = QStringLiteral("Embedded");
  working_color_profile_ = QStringLiteral("Linear ProPhoto RGB");
  preview_color_space_ = QStringLiteral("sRGB");
  monitor_color_profile_.clear();
  soft_proof_enabled_ = false;
  soft_proof_profile_ = QStringLiteral("sRGB");
  render_intent_ = QStringLiteral("Perceptual");
  raw_input_transform_enabled_ = true;
  highlight_reconstruction_mode_ = QStringLiteral("Enabled");
  chromatic_adaptation_method_ = QStringLiteral("Bradford");
  raw_camera_matching_look_ = QStringLiteral("Standard");
  output_color_space_ = QStringLiteral("sRGB");
  mask_stack_.clear();
  photo_layers_.clear();
  invalidateMaskDerivedCaches();
  active_mask_index_ = -1;
  linear_mask_enabled_ = false;
  linear_mask_exposure_ = 0.0;
  linear_mask_temperature_ = 0.0;
  linear_mask_feather_ = 0.22;
  linear_mask_position_ = 0.5;
  linear_mask_rotation_ = 0.0;
  radial_mask_enabled_ = false;
  radial_mask_exposure_ = 0.0;
  radial_mask_temperature_ = 0.0;
  radial_mask_feather_ = 0.25;
  radial_mask_center_x_ = 0.5;
  radial_mask_center_y_ = 0.5;
  radial_mask_radius_x_ = 0.25;
  radial_mask_radius_y_ = 0.25;
  radial_mask_rotation_ = 0.0;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

bool EditorPreviewController::resetAdjustmentGroup(const QString& group) {
  auto key = group.trimmed().toLower();
  key.remove(QLatin1Char(' '));
  key.remove(QLatin1Char('-'));
  key.remove(QLatin1Char('_'));

  const auto current_state = currentState();
  const EditState neutral_state;
  auto next_state = current_state;

  if (key == QStringLiteral("whitebalance") || key == QStringLiteral("wb")) {
    next_state.temperature = neutral_state.temperature;
    next_state.tint = neutral_state.tint;
  } else if (key == QStringLiteral("light") || key == QStringLiteral("tone")) {
    next_state.exposure = neutral_state.exposure;
    next_state.contrast = neutral_state.contrast;
    next_state.highlights = neutral_state.highlights;
    next_state.shadows = neutral_state.shadows;
    next_state.whites = neutral_state.whites;
    next_state.blacks = neutral_state.blacks;
    next_state.brightness = neutral_state.brightness;
    next_state.hdr_local_tone_mapping = neutral_state.hdr_local_tone_mapping;
    next_state.filmic_shoulder = neutral_state.filmic_shoulder;
    next_state.filmic_toe = neutral_state.filmic_toe;
    next_state.adaptive_contrast = neutral_state.adaptive_contrast;
    next_state.levels_black_point = neutral_state.levels_black_point;
    next_state.levels_gamma = neutral_state.levels_gamma;
    next_state.levels_white_point = neutral_state.levels_white_point;
    next_state.zone_deep_shadows = neutral_state.zone_deep_shadows;
    next_state.zone_shadows = neutral_state.zone_shadows;
    next_state.zone_midtones = neutral_state.zone_midtones;
    next_state.zone_lights = neutral_state.zone_lights;
    next_state.zone_highlights = neutral_state.zone_highlights;
  } else if (key == QStringLiteral("color")) {
    next_state.vibrance = neutral_state.vibrance;
    next_state.saturation = neutral_state.saturation;
  } else if (key == QStringLiteral("presence")) {
    next_state.texture = neutral_state.texture;
    next_state.clarity = neutral_state.clarity;
    next_state.dehaze = neutral_state.dehaze;
  } else if (key == QStringLiteral("detail")) {
    next_state.sharpening = neutral_state.sharpening;
    next_state.sharpen_masking = neutral_state.sharpen_masking;
    next_state.luminance_nr = neutral_state.luminance_nr;
    next_state.color_nr = neutral_state.color_nr;
  } else if (key == QStringLiteral("lens")) {
    next_state.lens_distortion = neutral_state.lens_distortion;
    next_state.lens_vignette = neutral_state.lens_vignette;
    next_state.chromatic_aberration = neutral_state.chromatic_aberration;
  } else if (key == QStringLiteral("raw")) {
    next_state.raw_exposure = neutral_state.raw_exposure;
    next_state.raw_boost = neutral_state.raw_boost;
    next_state.raw_shadow_boost = neutral_state.raw_shadow_boost;
    next_state.raw_detail = neutral_state.raw_detail;
    next_state.raw_highlight_recovery = neutral_state.raw_highlight_recovery;
    next_state.raw_lens_correction = neutral_state.raw_lens_correction;
  } else {
    return false;
  }

  if (edit_states_equal(current_state, next_state)) {
    return false;
  }

  rememberStateForUndo();
  applyHistoryState(next_state);
  return true;
}

QString EditorPreviewController::currentRecipeJson() const { return recipeJsonForState(currentState()); }

QString EditorPreviewController::copiedSettingsRecipeJson() const {
  if (!has_copied_settings_) {
    return {};
  }
  return recipeJsonForState(copied_settings_state_);
}

QString EditorPreviewController::presetRecipeJson(const QString& name) const {
  const auto normalized_name = normalizedPresetName(name);
  auto iterator = presets_.constFind(normalized_name);
  if (iterator != presets_.cend()) {
    return recipeJsonForState(iterator.value());
  }
  iterator = built_in_presets_.constFind(normalized_name);
  if (iterator == built_in_presets_.cend()) {
    return {};
  }
  return recipeJsonForState(iterator.value());
}

QString EditorPreviewController::presetPackName(const QString& name) const {
  const auto normalized_name = normalizedPresetName(name);
  if (presets_.contains(normalized_name)) {
    return user_preset_packs_.value(normalized_name, QStringLiteral("User Presets"));
  }
  return built_in_preset_packs_.value(normalized_name);
}

QString EditorPreviewController::presetCategoryName(const QString& name) const {
  const auto normalized_name = normalizedPresetName(name);
  if (presets_.contains(normalized_name)) {
    return user_preset_categories_.value(
        normalized_name,
        normalized_preset_category(user_preset_packs_.value(normalized_name), QStringLiteral("User")));
  }
  return built_in_preset_categories_.value(normalized_name);
}

QStringList EditorPreviewController::presetAdjustmentGroups(const QString& name) const {
  const auto normalized_name = normalizedPresetName(name);
  if (presets_.contains(normalized_name)) {
    const auto groups = user_preset_groups_.value(normalized_name);
    return groups.isEmpty() ? declared_preset_groups_for_state(presets_.value(normalized_name)) : groups;
  }
  const auto built_in_groups = built_in_preset_groups_.value(normalized_name);
  if (!built_in_groups.isEmpty()) {
    return built_in_groups;
  }
  const auto built_in_iterator = built_in_presets_.constFind(normalized_name);
  return built_in_iterator == built_in_presets_.cend()
             ? QStringList{}
             : declared_preset_groups_for_state(built_in_iterator.value());
}

QStringList EditorPreviewController::presetSafetyWarnings(
    const QString& name,
    const QStringList& adjustment_groups) const {
  const auto normalized_name = normalizedPresetName(name);
  const auto user_iterator = presets_.constFind(normalized_name);
  const auto built_in_iterator = built_in_presets_.constFind(normalized_name);
  const auto* preset_state = user_iterator != presets_.cend()
                                 ? &user_iterator.value()
                                 : (built_in_iterator != built_in_presets_.cend() ? &built_in_iterator.value()
                                                                                  : nullptr);
  if (preset_state == nullptr) {
    return {};
  }

  const auto apply_groups =
      adjustment_groups.isEmpty() ? presetAdjustmentGroups(normalized_name) : adjustment_groups;
  return preset_safety_warnings_for_state(*preset_state, apply_groups);
}

QStringList EditorPreviewController::presetChangedControls(
    const QString& name,
    const QStringList& adjustment_groups) const {
  const auto normalized_name = normalizedPresetName(name);
  const auto user_iterator = presets_.constFind(normalized_name);
  const auto built_in_iterator = built_in_presets_.constFind(normalized_name);
  const auto* preset_state = user_iterator != presets_.cend()
                                 ? &user_iterator.value()
                                 : (built_in_iterator != built_in_presets_.cend() ? &built_in_iterator.value()
                                                                                  : nullptr);
  if (preset_state == nullptr) {
    return {};
  }

  const auto adaptive_rules = user_iterator != presets_.cend()
                                  ? user_preset_adaptive_rules_.value(normalized_name)
                                  : built_in_preset_adaptive_rules_.value(normalized_name);
  const auto resolved_preset_state = adaptive_rules.isEmpty()
                                         ? *preset_state
                                         : adaptive_preset_state_for_context(
                                               *preset_state,
                                               adaptive_rules,
                                               camera_label_,
                                               lens_model_,
                                               capture_date_,
                                               raw_highlight_clip_count_,
                                               raw_shadow_clip_count_);
  const auto apply_groups =
      adjustment_groups.isEmpty() ? presetAdjustmentGroups(normalized_name) : adjustment_groups;
  const auto base_state = currentState();
  const auto merged_state = merge_edit_recipe_state_by_groups(base_state, resolved_preset_state, apply_groups);
  const auto scaled_state = interpolate_preset_state(base_state, merged_state, preset_intensity_);
  return changed_controls_between_states(base_state, scaled_state);
}

bool EditorPreviewController::isPresetAdaptive(const QString& name) const {
  const auto normalized_name = normalizedPresetName(name);
  return built_in_preset_adaptive_rules_.contains(normalized_name) ||
         user_preset_adaptive_rules_.contains(normalized_name);
}

bool EditorPreviewController::isUserPreset(const QString& name) const {
  const auto normalized_name = normalizedPresetName(name);
  return !normalized_name.isEmpty() && presets_.contains(normalized_name);
}

QString EditorPreviewController::presetThumbnailSource(const QString& name) const {
  const auto normalized_name = normalizedPresetName(name);
  const auto user_iterator = presets_.constFind(normalized_name);
  if (user_iterator != presets_.cend()) {
    return preset_thumbnail_source_for_state(user_iterator.value());
  }
  const auto built_in_iterator = built_in_presets_.constFind(normalized_name);
  if (built_in_iterator != built_in_presets_.cend()) {
    return preset_thumbnail_source_for_state(built_in_iterator.value());
  }
  return {};
}

QStringList EditorPreviewController::searchPresetNames(const QString& query) const {
  const auto normalized_query = query.simplified();
  if (normalized_query.isEmpty()) {
    return presetNames();
  }

  QStringList matches;
  for (const auto& name : presetNames()) {
    const auto pack = presetPackName(name);
    const auto category = presetCategoryName(name);
    if (name.contains(normalized_query, Qt::CaseInsensitive) ||
        pack.contains(normalized_query, Qt::CaseInsensitive) ||
        category.contains(normalized_query, Qt::CaseInsensitive)) {
      matches.push_back(name);
    }
  }
  matches.sort(Qt::CaseInsensitive);
  return matches;
}

bool EditorPreviewController::isPresetFavorite(const QString& name) const {
  const auto normalized_name = normalizedPresetName(name);
  return !normalized_name.isEmpty() && favorite_presets_.contains(normalized_name);
}

void EditorPreviewController::setPresetFavorite(const QString& name, bool favorite) {
  const auto normalized_name = normalizedPresetName(name);
  if (normalized_name.isEmpty() ||
      (!presets_.contains(normalized_name) && !built_in_presets_.contains(normalized_name))) {
    return;
  }

  const auto had_favorite = favorite_presets_.contains(normalized_name);
  if (favorite == had_favorite) {
    return;
  }
  if (favorite) {
    favorite_presets_.insert(normalized_name);
  } else {
    favorite_presets_.remove(normalized_name);
  }
  savePresets();
  emit presetsChanged();
}

bool EditorPreviewController::copyCurrentSettings() {
  if (asset_id_.isEmpty() && !hasAsset()) {
    return false;
  }

  copied_settings_state_ = currentState();
  if (!has_copied_settings_) {
    has_copied_settings_ = true;
    emit clipboardChanged();
  }
  setStatus(QStringLiteral("Copied current adjustments."));
  return true;
}

bool EditorPreviewController::pasteCopiedSettings(const QStringList& adjustment_groups) {
  if ((asset_id_.isEmpty() && !hasAsset()) || !has_copied_settings_) {
    return false;
  }

  const auto merged_state = merge_edit_recipe_state_by_groups(currentState(), copied_settings_state_, adjustment_groups);
  if (edit_states_equal(merged_state, currentState())) {
    return false;
  }

  rememberStateForUndo();
  applyState(merged_state);
  persistCurrentState();
  renderPreview();
  setStatus(QStringLiteral("Pasted copied adjustments."));
  return true;
}

bool EditorPreviewController::savePreset(const QString& name, bool overwrite) {
  return savePresetToPack(name, QStringLiteral("User Presets"), overwrite);
}

bool EditorPreviewController::savePresetToPack(const QString& name, const QString& pack, bool overwrite) {
  const auto normalized_name = normalizedPresetName(name);
  if (normalized_name.isEmpty() || (asset_id_.isEmpty() && !hasAsset())) {
    return false;
  }
  const auto normalized_pack =
      pack.simplified().isEmpty() ? QStringLiteral("User Presets") : pack.simplified();

  const auto preset_exists = presets_.contains(normalized_name);
  if (preset_exists && !overwrite) {
    return false;
  }

  const auto names_before = presetNames();
  const auto packs_before = presetPackNames();
  const auto categories_before = presetCategoryNames();
  const auto preset_state = currentState();
  presets_.insert(normalized_name, preset_state);
  user_preset_packs_.insert(normalized_name, normalized_pack);
  user_preset_categories_.insert(normalized_name, normalized_pack);
  user_preset_groups_.insert(normalized_name, declared_preset_groups_for_state(preset_state));
  savePresets();
  if (names_before != presetNames() || packs_before != presetPackNames() ||
      categories_before != presetCategoryNames()) {
    emit presetsChanged();
  }
  setStatus(!preset_exists
                ? QStringLiteral("Saved preset '%1' to %2.").arg(normalized_name, normalized_pack)
                : QStringLiteral("Updated preset '%1' in %2.").arg(normalized_name, normalized_pack));
  return true;
}

bool EditorPreviewController::applyPreset(const QString& name, const QStringList& adjustment_groups) {
  return applyPresetWithIntensity(name, preset_intensity_, adjustment_groups);
}

bool EditorPreviewController::applyPresetWithIntensity(
    const QString& name,
    double intensity,
    const QStringList& adjustment_groups) {
  if (asset_id_.isEmpty() && !hasAsset()) {
    return false;
  }

  const auto normalized_name = normalizedPresetName(name);
  const auto user_iterator = presets_.constFind(normalized_name);
  const auto built_in_iterator = built_in_presets_.constFind(normalized_name);
  const auto* preset_state = user_iterator != presets_.cend()
                                 ? &user_iterator.value()
                                 : (built_in_iterator != built_in_presets_.cend() ? &built_in_iterator.value()
                                                                                  : nullptr);
  if (preset_state == nullptr) {
    return false;
  }

  const auto apply_groups =
      adjustment_groups.isEmpty() ? presetAdjustmentGroups(normalized_name) : adjustment_groups;
  const auto adaptive_rules = user_iterator != presets_.cend()
                                  ? user_preset_adaptive_rules_.value(normalized_name)
                                  : built_in_preset_adaptive_rules_.value(normalized_name);
  const auto resolved_preset_state = adaptive_rules.isEmpty()
                                         ? *preset_state
                                         : adaptive_preset_state_for_context(
                                               *preset_state,
                                               adaptive_rules,
                                               camera_label_,
                                               lens_model_,
                                               capture_date_,
                                               raw_highlight_clip_count_,
                                               raw_shadow_clip_count_);
  const auto base_state = currentState();
  const auto merged_state = merge_edit_recipe_state_by_groups(base_state, resolved_preset_state, apply_groups);
  const auto scaled_state = interpolate_preset_state(base_state, merged_state, intensity);
  if (edit_states_equal(scaled_state, base_state)) {
    return false;
  }

  rememberStateForUndo();
  applyState(scaled_state);
  persistCurrentState();
  renderPreview();
  setStatus(
      qBound(0.0, intensity, 1.0) >= 1.0 - 1e-6
          ? QStringLiteral("Applied preset '%1'.").arg(normalized_name)
          : QStringLiteral("Applied preset '%1' at %2%.")
                .arg(normalized_name)
                .arg(static_cast<int>(std::lround(qBound(0.0, intensity, 1.0) * 100.0))));
  return true;
}

QString EditorPreviewController::presetPreviewRecipeJson(
    const QString& name,
    const QStringList& adjustment_groups,
    double intensity) const {
  const auto normalized_name = normalizedPresetName(name);
  const auto user_iterator = presets_.constFind(normalized_name);
  const auto built_in_iterator = built_in_presets_.constFind(normalized_name);
  const auto* preset_state = user_iterator != presets_.cend()
                                 ? &user_iterator.value()
                                 : (built_in_iterator != built_in_presets_.cend() ? &built_in_iterator.value()
                                                                                  : nullptr);
  if (preset_state == nullptr) {
    return {};
  }

  const auto apply_groups =
      adjustment_groups.isEmpty() ? presetAdjustmentGroups(normalized_name) : adjustment_groups;
  const auto adaptive_rules = user_iterator != presets_.cend()
                                  ? user_preset_adaptive_rules_.value(normalized_name)
                                  : built_in_preset_adaptive_rules_.value(normalized_name);
  const auto resolved_preset_state = adaptive_rules.isEmpty()
                                         ? *preset_state
                                         : adaptive_preset_state_for_context(
                                               *preset_state,
                                               adaptive_rules,
                                               camera_label_,
                                               lens_model_,
                                               capture_date_,
                                               raw_highlight_clip_count_,
                                               raw_shadow_clip_count_);
  const auto base_state = currentState();
  const auto merged_state = merge_edit_recipe_state_by_groups(base_state, resolved_preset_state, apply_groups);
  const auto preview_intensity = intensity < 0.0 ? preset_intensity_ : intensity;
  return recipeJsonForState(interpolate_preset_state(base_state, merged_state, preview_intensity));
}

bool EditorPreviewController::renamePreset(const QString& old_name, const QString& new_name) {
  const auto normalized_old_name = normalizedPresetName(old_name);
  const auto normalized_new_name = normalizedPresetName(new_name);
  if (normalized_old_name.isEmpty() || normalized_new_name.isEmpty() ||
      !presets_.contains(normalized_old_name) || built_in_presets_.contains(normalized_new_name)) {
    return false;
  }
  if (normalized_old_name == normalized_new_name) {
    return true;
  }
  if (presets_.contains(normalized_new_name)) {
    return false;
  }

  presets_.insert(normalized_new_name, presets_.take(normalized_old_name));
  user_preset_packs_.insert(
      normalized_new_name,
      user_preset_packs_.take(normalized_old_name));
  user_preset_categories_.insert(
      normalized_new_name,
      user_preset_categories_.take(normalized_old_name));
  user_preset_groups_.insert(normalized_new_name, user_preset_groups_.take(normalized_old_name));
  if (user_preset_adaptive_rules_.contains(normalized_old_name)) {
    user_preset_adaptive_rules_.insert(
        normalized_new_name,
        user_preset_adaptive_rules_.take(normalized_old_name));
  }
  if (favorite_presets_.remove(normalized_old_name) > 0) {
    favorite_presets_.insert(normalized_new_name);
  }

  savePresets();
  emit presetsChanged();
  setStatus(QStringLiteral("Renamed preset '%1' to '%2'.").arg(normalized_old_name, normalized_new_name));
  return true;
}

bool EditorPreviewController::movePresetToPack(const QString& name, const QString& pack) {
  const auto normalized_name = normalizedPresetName(name);
  if (normalized_name.isEmpty() || !presets_.contains(normalized_name)) {
    return false;
  }
  const auto normalized_pack =
      pack.simplified().isEmpty() ? QStringLiteral("User Presets") : pack.simplified();
  const auto current_pack = user_preset_packs_.value(normalized_name, QStringLiteral("User Presets"));
  const auto current_category = user_preset_categories_.value(
      normalized_name,
      normalized_preset_category(current_pack, QStringLiteral("User")));
  if (current_pack == normalized_pack && current_category == normalized_pack) {
    return true;
  }

  user_preset_packs_.insert(normalized_name, normalized_pack);
  user_preset_categories_.insert(normalized_name, normalized_pack);
  savePresets();
  emit presetsChanged();
  setStatus(QStringLiteral("Moved preset '%1' to %2.").arg(normalized_name, normalized_pack));
  return true;
}

bool EditorPreviewController::deletePreset(const QString& name) {
  const auto normalized_name = normalizedPresetName(name);
  if (normalized_name.isEmpty() || !presets_.contains(normalized_name)) {
    return false;
  }

  presets_.remove(normalized_name);
  user_preset_packs_.remove(normalized_name);
  user_preset_categories_.remove(normalized_name);
  user_preset_groups_.remove(normalized_name);
  user_preset_adaptive_rules_.remove(normalized_name);
  favorite_presets_.remove(normalized_name);
  savePresets();
  emit presetsChanged();
  setStatus(QStringLiteral("Deleted preset '%1'.").arg(normalized_name));
  return true;
}

bool EditorPreviewController::exportPresetPack(const QString& file_path, const QString& pack) const {
  const auto normalized_path = file_path.trimmed();
  if (normalized_path.isEmpty()) {
    return false;
  }
  const auto normalized_pack = pack.simplified();
  QJsonArray preset_array;
  for (auto iterator = presets_.cbegin(); iterator != presets_.cend(); ++iterator) {
    const auto preset_pack = user_preset_packs_.value(iterator.key(), QStringLiteral("User Presets"));
    if (!normalized_pack.isEmpty() && preset_pack.compare(normalized_pack, Qt::CaseInsensitive) != 0) {
      continue;
    }
    QJsonObject preset_object;
    preset_object.insert(QStringLiteral("name"), iterator.key());
    preset_object.insert(QStringLiteral("pack"), preset_pack);
    preset_object.insert(
        QStringLiteral("category"),
        user_preset_categories_.value(iterator.key(), normalized_preset_category(preset_pack, QStringLiteral("User"))));
    preset_object.insert(
        QStringLiteral("adjustmentGroups"),
        preset_adjustment_groups_to_json(
            user_preset_groups_.value(iterator.key(), declared_preset_groups_for_state(iterator.value()))));
    const auto adaptive_rules = user_preset_adaptive_rules_.value(iterator.key());
    if (!adaptive_rules.isEmpty()) {
      preset_object.insert(QStringLiteral("adaptiveRules"), adaptive_rules);
    }
    preset_object.insert(QStringLiteral("recipeJson"), recipeJsonForState(iterator.value()));
    preset_array.push_back(preset_object);
  }
  if (preset_array.isEmpty()) {
    return false;
  }

  const QFileInfo destination_info(normalized_path);
  QDir().mkpath(destination_info.dir().absolutePath());
  QJsonObject root;
  root.insert(QStringLiteral("format"), QStringLiteral("photo-editor-preset-pack"));
  root.insert(QStringLiteral("version"), 1);
  root.insert(QStringLiteral("presets"), preset_array);
  QSaveFile file(normalized_path);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  return file.commit();
}

bool EditorPreviewController::importPresetPack(const QString& file_path, bool overwrite) {
  QFile file(file_path.trimmed());
  if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
    return false;
  }
  const auto document = QJsonDocument::fromJson(file.readAll());
  if (!document.isObject()) {
    return false;
  }

  auto changed = false;
  const auto names_before = presetNames();
  const auto packs_before = presetPackNames();
  const auto categories_before = presetCategoryNames();
  const auto preset_array = document.object().value(QStringLiteral("presets")).toArray();
  for (const auto& preset_value : preset_array) {
    if (!preset_value.isObject()) {
      continue;
    }
    const auto preset_object = preset_value.toObject();
    const auto name = normalizedPresetName(preset_object.value(QStringLiteral("name")).toString());
    const auto recipe_json = preset_object.value(QStringLiteral("recipeJson")).toString();
    const auto pack = preset_object.value(QStringLiteral("pack")).toString(QStringLiteral("Imported Presets")).simplified();
    const auto category =
        normalized_preset_category(preset_object.value(QStringLiteral("category")).toString(), pack);
    const auto groups = preset_adjustment_groups_from_json(
        preset_object.value(QStringLiteral("adjustmentGroups")).toArray());
    const auto adaptive_rules = preset_object.value(QStringLiteral("adaptiveRules")).toArray();
    if (name.isEmpty() || recipe_json.trimmed().isEmpty() || (presets_.contains(name) && !overwrite)) {
      continue;
    }
    const auto preset_state = stateFromRecipeJson(recipe_json);
    presets_.insert(name, preset_state);
    user_preset_packs_.insert(name, pack.isEmpty() ? QStringLiteral("Imported Presets") : pack);
    user_preset_categories_.insert(name, category);
    user_preset_groups_.insert(
        name,
        groups.isEmpty() ? declared_preset_groups_for_state(preset_state) : groups);
    if (!adaptive_rules.isEmpty()) {
      user_preset_adaptive_rules_.insert(name, adaptive_rules);
    } else {
      user_preset_adaptive_rules_.remove(name);
    }
    changed = true;
  }
  if (!changed) {
    return false;
  }

  savePresets();
  if (names_before != presetNames() || packs_before != presetPackNames() ||
      categories_before != presetCategoryNames()) {
    emit presetsChanged();
  }
  setStatus(QStringLiteral("Imported preset pack."));
  return true;
}

QString EditorPreviewController::presetSyncManifestJson() const {
  QJsonArray preset_array;
  for (auto iterator = presets_.cbegin(); iterator != presets_.cend(); ++iterator) {
    const auto preset_pack = user_preset_packs_.value(iterator.key(), QStringLiteral("User Presets"));
    QJsonObject preset_object;
    preset_object.insert(QStringLiteral("name"), iterator.key());
    preset_object.insert(QStringLiteral("pack"), preset_pack);
    preset_object.insert(
        QStringLiteral("category"),
        user_preset_categories_.value(
            iterator.key(),
            normalized_preset_category(preset_pack, QStringLiteral("User"))));
    preset_object.insert(
        QStringLiteral("adjustmentGroups"),
        preset_adjustment_groups_to_json(
            user_preset_groups_.value(iterator.key(), declared_preset_groups_for_state(iterator.value()))));
    const auto adaptive_rules = user_preset_adaptive_rules_.value(iterator.key());
    if (!adaptive_rules.isEmpty()) {
      preset_object.insert(QStringLiteral("adaptiveRules"), adaptive_rules);
    }
    preset_object.insert(QStringLiteral("recipeJson"), recipeJsonForState(iterator.value()));
    preset_array.push_back(preset_object);
  }

  QJsonArray favorite_array;
  for (const auto& name : favoritePresetNames()) {
    favorite_array.push_back(name);
  }

  QJsonObject root;
  root.insert(QStringLiteral("format"), QStringLiteral("photo-editor-preset-sync"));
  root.insert(QStringLiteral("version"), 1);
  root.insert(QStringLiteral("generatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
  root.insert(QStringLiteral("presets"), preset_array);
  root.insert(QStringLiteral("favorites"), favorite_array);
  return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

bool EditorPreviewController::exportPresetSyncBundle(const QString& folder_path) const {
  const auto manifest_path = preset_sync_manifest_file_path(folder_path);
  if (manifest_path.isEmpty()) {
    return false;
  }
  QDir().mkpath(QFileInfo(manifest_path).dir().absolutePath());
  QSaveFile file(manifest_path);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  file.write(presetSyncManifestJson().toUtf8());
  return file.commit();
}

bool EditorPreviewController::importPresetSyncBundle(const QString& folder_path, bool overwrite) {
  const auto manifest_path = preset_sync_manifest_file_path(folder_path);
  if (manifest_path.isEmpty()) {
    return false;
  }
  QFile file(manifest_path);
  if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
    return false;
  }

  const auto document = QJsonDocument::fromJson(file.readAll());
  if (!document.isObject()) {
    return false;
  }
  const auto root = document.object();
  if (root.value(QStringLiteral("format")).toString() != QStringLiteral("photo-editor-preset-sync")) {
    return false;
  }

  auto changed = false;
  const auto preset_array = root.value(QStringLiteral("presets")).toArray();
  for (const auto& preset_value : preset_array) {
    if (!preset_value.isObject()) {
      continue;
    }
    const auto preset_object = preset_value.toObject();
    const auto name = normalizedPresetName(preset_object.value(QStringLiteral("name")).toString());
    const auto recipe_json = preset_object.value(QStringLiteral("recipeJson")).toString();
    if (name.isEmpty() || recipe_json.trimmed().isEmpty() || (presets_.contains(name) && !overwrite)) {
      continue;
    }

    const auto pack = preset_object.value(QStringLiteral("pack")).toString(QStringLiteral("Synced Presets")).simplified();
    const auto category =
        normalized_preset_category(preset_object.value(QStringLiteral("category")).toString(), pack);
    const auto groups = preset_adjustment_groups_from_json(
        preset_object.value(QStringLiteral("adjustmentGroups")).toArray());
    const auto adaptive_rules = preset_object.value(QStringLiteral("adaptiveRules")).toArray();
    const auto preset_state = stateFromRecipeJson(recipe_json);
    presets_.insert(name, preset_state);
    user_preset_packs_.insert(name, pack.isEmpty() ? QStringLiteral("Synced Presets") : pack);
    user_preset_categories_.insert(name, category);
    user_preset_groups_.insert(
        name,
        groups.isEmpty() ? declared_preset_groups_for_state(preset_state) : groups);
    if (!adaptive_rules.isEmpty()) {
      user_preset_adaptive_rules_.insert(name, adaptive_rules);
    } else {
      user_preset_adaptive_rules_.remove(name);
    }
    changed = true;
  }

  for (const auto& favorite_value : root.value(QStringLiteral("favorites")).toArray()) {
    const auto name = normalizedPresetName(favorite_value.toString());
    if (!name.isEmpty() && (presets_.contains(name) || built_in_presets_.contains(name))) {
      favorite_presets_.insert(name);
      changed = true;
    }
  }

  if (!changed) {
    return false;
  }
  savePresets();
  emit presetsChanged();
  setStatus(QStringLiteral("Imported preset sync bundle."));
  return true;
}

bool EditorPreviewController::applyCurvePreset(const QString& name) {
  const auto preset_points = curve_preset_points(name);
  if (preset_points.isEmpty()) {
    return false;
  }

  const auto normalized_points = normalize_curve_points(preset_points);
  auto* target_points = &blue_curve_points_;
  if (curve_channel_ == QStringLiteral("RGB") || curve_channel_ == QStringLiteral("Master")) {
    target_points = &master_curve_points_;
  } else if (curve_channel_ == QStringLiteral("Luma")) {
    target_points = &luma_curve_points_;
  } else if (curve_channel_ == QStringLiteral("Red")) {
    target_points = &red_curve_points_;
  } else if (curve_channel_ == QStringLiteral("Green")) {
    target_points = &green_curve_points_;
  }

  if (*target_points == normalized_points) {
    return true;
  }

  rememberStateForUndo();
  *target_points = normalized_points;
  if (target_points == &master_curve_points_) {
    curve_shadows_ = 0.0;
    curve_darks_ = 0.0;
    curve_lights_ = 0.0;
    curve_highlights_ = 0.0;
  }

  persistCurrentState();
  emit controlsChanged();
  renderPreview();
  setStatus(QStringLiteral("Applied curve preset '%1'.").arg(name.trimmed()));
  return true;
}

QVariantList EditorPreviewController::curvePresetPreviewPoints(const QString& name) const {
  return curve_preset_points(name);
}

bool EditorPreviewController::saveCameraDefault() {
  const auto key = normalizedCameraDefaultKey(camera_label_);
  if (key.isEmpty() || (asset_id_.isEmpty() && !hasAsset())) {
    return false;
  }

  const auto default_state = merge_edit_recipe_state_by_groups(
      EditState{},
      currentState(),
      {QStringLiteral("raw"), QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")});
  const auto had_default = camera_default_states_.contains(key);
  camera_default_states_.insert(key, default_state);
  saveCameraDefaults();
  emit cameraDefaultsChanged();
  setStatus(had_default ? QStringLiteral("Updated camera default for %1.").arg(camera_label_.simplified())
                        : QStringLiteral("Saved camera default for %1.").arg(camera_label_.simplified()));
  return true;
}

bool EditorPreviewController::clearCameraDefault() {
  const auto key = normalizedCameraDefaultKey(camera_label_);
  if (key.isEmpty() || !camera_default_states_.contains(key)) {
    return false;
  }

  camera_default_states_.remove(key);
  saveCameraDefaults();
  emit cameraDefaultsChanged();
  setStatus(QStringLiteral("Cleared camera default for %1.").arg(camera_label_.simplified()));
  return true;
}

bool EditorPreviewController::addAdjustmentLayer(const QString& name) {
  rememberStateForUndo();
  auto next_layers = photo_layers_;
  next_layers.prepend(adjustment_layer_from_state(
      currentState(),
      name,
      static_cast<int>(next_layers.size()) + 1));
  photo_layers_ = normalized_photo_layers(next_layers);

  exposure_ = 0.0;
  contrast_ = 0.0;
  highlights_ = 0.0;
  shadows_ = 0.0;
  whites_ = 0.0;
  blacks_ = 0.0;
  temperature_ = 0.0;
  tint_ = 0.0;
  vibrance_ = 0.0;
  saturation_ = 0.0;
  hue_adjustment_ = 0.0;
  saturation_adjustment_ = 0.0;

  persistCurrentState();
  emit controlsChanged();
  renderPreview();
  return true;
}

bool EditorPreviewController::addFillLayer(const QString& color, const QString& name) {
  rememberStateForUndo();
  auto next_layers = photo_layers_;
  next_layers.prepend(fill_layer_definition(color, name, static_cast<int>(next_layers.size()) + 1));
  photo_layers_ = normalized_photo_layers(next_layers);
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
  return true;
}

bool EditorPreviewController::addTextLayer(const QString& text, const QString& name) {
  rememberStateForUndo();
  auto next_layers = photo_layers_;
  next_layers.prepend(text_layer_definition(text, name, static_cast<int>(next_layers.size()) + 1));
  photo_layers_ = normalized_photo_layers(next_layers);
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
  return true;
}

bool EditorPreviewController::addSourceLayer(const QString& file_path, const QString& name) {
  const QFileInfo source_info(file_path);
  if (!source_info.exists() || !source_info.isReadable()) {
    setStatus(QStringLiteral("Choose a readable source image for the layer."));
    return false;
  }
  rememberStateForUndo();
  auto next_layers = photo_layers_;
  next_layers.prepend(source_layer_definition(file_path, name, static_cast<int>(next_layers.size()) + 1));
  photo_layers_ = normalized_photo_layers(next_layers);
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
  return true;
}

bool EditorPreviewController::addLayerGroup(const QString& name) {
  rememberStateForUndo();
  auto next_layers = photo_layers_;
  next_layers.prepend(layer_group_definition(name, static_cast<int>(next_layers.size()) + 1));
  photo_layers_ = normalized_photo_layers(next_layers);
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
  return true;
}

bool EditorPreviewController::setPhotoLayerEnabled(int index, bool enabled) {
  if (index < 0 || index >= photo_layers_.size()) {
    return false;
  }
  auto next_layers = photo_layers_;
  auto layer = next_layers.at(index).toMap();
  if (layer.value(QStringLiteral("locked")).toBool() ||
      (layer.contains(QStringLiteral("enabled"))
           ? layer.value(QStringLiteral("enabled")).toBool()
           : true) == enabled) {
    return false;
  }
  layer.insert(QStringLiteral("enabled"), enabled);
  next_layers[index] = layer;
  return applyPhotoLayerStackChange(next_layers);
}

bool EditorPreviewController::setPhotoLayerLocked(int index, bool locked) {
  if (index < 0 || index >= photo_layers_.size()) {
    return false;
  }
  auto next_layers = photo_layers_;
  auto layer = next_layers.at(index).toMap();
  if (layer.value(QStringLiteral("locked")).toBool() == locked) {
    return false;
  }
  layer.insert(QStringLiteral("locked"), locked);
  next_layers[index] = layer;
  return applyPhotoLayerStackChange(next_layers);
}

bool EditorPreviewController::setPhotoLayerSolo(int index, bool solo) {
  if (index < 0 || index >= photo_layers_.size()) {
    return false;
  }
  auto next_layers = photo_layers_;
  auto layer = next_layers.at(index).toMap();
  if (layer.value(QStringLiteral("locked")).toBool() ||
      layer.value(QStringLiteral("solo")).toBool() == solo) {
    return false;
  }
  layer.insert(QStringLiteral("solo"), solo);
  next_layers[index] = layer;
  return applyPhotoLayerStackChange(next_layers);
}

bool EditorPreviewController::setPhotoLayerOpacity(int index, double opacity) {
  if (index < 0 || index >= photo_layers_.size()) {
    return false;
  }
  auto next_layers = photo_layers_;
  auto layer = next_layers.at(index).toMap();
  if (layer.value(QStringLiteral("locked")).toBool()) {
    return false;
  }
  const auto next_opacity = qBound(0.0, opacity, 1.0);
  if (std::abs(layer.value(QStringLiteral("opacity"), 1.0).toDouble() - next_opacity) < 0.0001) {
    return false;
  }
  layer.insert(QStringLiteral("opacity"), next_opacity);
  next_layers[index] = layer;
  return applyPhotoLayerStackChange(next_layers);
}

bool EditorPreviewController::setPhotoLayerBlendMode(int index, const QString& blend_mode) {
  if (index < 0 || index >= photo_layers_.size()) {
    return false;
  }
  auto next_layers = photo_layers_;
  auto layer = next_layers.at(index).toMap();
  if (layer.value(QStringLiteral("locked")).toBool()) {
    return false;
  }
  const auto next_blend_mode = normalized_mask_blend_mode(blend_mode);
  if (layer.value(QStringLiteral("blendMode")).toString() == next_blend_mode) {
    return false;
  }
  layer.insert(QStringLiteral("blendMode"), next_blend_mode);
  next_layers[index] = layer;
  return applyPhotoLayerStackChange(next_layers);
}

bool EditorPreviewController::setPhotoLayerClippingMask(int index, bool clipping_mask) {
  if (index < 0 || index >= photo_layers_.size()) {
    return false;
  }
  auto next_layers = photo_layers_;
  auto layer = next_layers.at(index).toMap();
  if (layer.value(QStringLiteral("locked")).toBool() ||
      layer.value(QStringLiteral("kind")).toString() == QStringLiteral("group") ||
      (clipping_mask && index >= photo_layers_.size() - 1) ||
      layer.value(QStringLiteral("clippingMask")).toBool() == clipping_mask) {
    return false;
  }
  layer.insert(QStringLiteral("clippingMask"), clipping_mask);
  next_layers[index] = layer;
  return applyPhotoLayerStackChange(next_layers);
}

bool EditorPreviewController::setPhotoLayerMaskEnabled(int index, bool enabled) {
  if (index < 0 || index >= photo_layers_.size()) {
    return false;
  }
  auto next_layers = photo_layers_;
  auto layer = next_layers.at(index).toMap();
  const auto has_mask =
      layer.contains(QStringLiteral("maskData")) || layer.value(QStringLiteral("maskWidth")).toInt() > 0;
  if (layer.value(QStringLiteral("locked")).toBool() || has_mask == enabled) {
    return false;
  }
  if (enabled) {
    layer.insert(QStringLiteral("maskData"), QByteArray(1, char(0xff)));
    layer.insert(QStringLiteral("maskWidth"), 1);
    layer.insert(QStringLiteral("maskHeight"), 1);
    layer.insert(QStringLiteral("maskInverted"), false);
  } else {
    layer.remove(QStringLiteral("maskData"));
    layer.remove(QStringLiteral("maskWidth"));
    layer.remove(QStringLiteral("maskHeight"));
    layer.remove(QStringLiteral("maskInverted"));
  }
  next_layers[index] = layer;
  return applyPhotoLayerStackChange(next_layers);
}

bool EditorPreviewController::isolatePhotoLayer(int index) {
  if (index < 0 || index >= photo_layers_.size()) {
    return false;
  }
  auto next_layers = photo_layers_;
  auto changed = false;
  for (int layer_index = 0; layer_index < next_layers.size(); ++layer_index) {
    auto layer = next_layers.at(layer_index).toMap();
    const auto should_solo = layer_index == index;
    if (should_solo && layer.value(QStringLiteral("locked")).toBool()) {
      return false;
    }
    if (layer.value(QStringLiteral("solo")).toBool() == should_solo) {
      continue;
    }
    layer.insert(QStringLiteral("solo"), should_solo);
    next_layers[layer_index] = layer;
    changed = true;
  }
  return changed && applyPhotoLayerStackChange(next_layers);
}

bool EditorPreviewController::clearPhotoLayerSolo() {
  auto next_layers = photo_layers_;
  auto changed = false;
  for (int index = 0; index < next_layers.size(); ++index) {
    auto layer = next_layers.at(index).toMap();
    if (!layer.value(QStringLiteral("solo")).toBool()) {
      continue;
    }
    layer.insert(QStringLiteral("solo"), false);
    next_layers[index] = layer;
    changed = true;
  }
  return changed && applyPhotoLayerStackChange(next_layers);
}

bool EditorPreviewController::savePhotoLayerVisibilityState(const QString& name) {
  if (photo_layers_.isEmpty()) {
    return false;
  }

  QVariantList layer_states;
  auto visible_count = 0;
  auto solo_count = 0;
  for (const auto& value : photo_layers_) {
    const auto layer = value.toMap();
    const auto enabled = layer.contains(QStringLiteral("enabled"))
                             ? layer.value(QStringLiteral("enabled")).toBool()
                             : true;
    const auto solo = layer.value(QStringLiteral("solo")).toBool();
    QVariantMap layer_state;
    layer_state.insert(QStringLiteral("id"), layer.value(QStringLiteral("id")).toString());
    layer_state.insert(QStringLiteral("name"), layer.value(QStringLiteral("name")).toString());
    layer_state.insert(QStringLiteral("enabled"), enabled);
    layer_state.insert(QStringLiteral("solo"), solo);
    layer_states.push_back(layer_state);
    if (enabled) {
      ++visible_count;
    }
    if (solo) {
      ++solo_count;
    }
  }

  const auto normalized_name = name.simplified().isEmpty()
                                   ? QStringLiteral("Visibility %1").arg(photo_layer_visibility_states_.size() + 1)
                                   : name.simplified();
  QVariantMap state;
  state.insert(QStringLiteral("name"), normalized_name);
  state.insert(QStringLiteral("layerCount"), photo_layers_.size());
  state.insert(QStringLiteral("visibleCount"), visible_count);
  state.insert(QStringLiteral("soloCount"), solo_count);
  state.insert(
      QStringLiteral("visibilitySummary"),
      QStringLiteral("%1/%2 visible").arg(visible_count).arg(photo_layers_.size()));
  state.insert(QStringLiteral("layers"), layer_states);
  photo_layer_visibility_states_.push_back(state);
  setStatus(QStringLiteral("Saved layer visibility state '%1'.").arg(normalized_name));
  emit controlsChanged();
  return true;
}

bool EditorPreviewController::applyPhotoLayerVisibilityState(int index) {
  if (index < 0 || index >= photo_layer_visibility_states_.size()) {
    return false;
  }
  const auto state = photo_layer_visibility_states_.at(index).toMap();
  const auto saved_layers = state.value(QStringLiteral("layers")).toList();
  if (saved_layers.isEmpty()) {
    return false;
  }

  QHash<QString, QVariantMap> saved_by_id;
  for (const auto& value : saved_layers) {
    const auto saved_layer = value.toMap();
    const auto id = saved_layer.value(QStringLiteral("id")).toString();
    if (!id.isEmpty()) {
      saved_by_id.insert(id, saved_layer);
    }
  }

  auto next_layers = photo_layers_;
  auto changed = false;
  for (int layer_index = 0; layer_index < next_layers.size(); ++layer_index) {
    auto layer = next_layers.at(layer_index).toMap();
    if (layer.value(QStringLiteral("locked")).toBool()) {
      continue;
    }
    const auto id = layer.value(QStringLiteral("id")).toString();
    if (id.isEmpty() || !saved_by_id.contains(id)) {
      continue;
    }
    const auto saved_layer = saved_by_id.value(id);
    const auto next_enabled = saved_layer.value(QStringLiteral("enabled"), true).toBool();
    const auto next_solo = saved_layer.value(QStringLiteral("solo")).toBool();
    const auto current_enabled = layer.contains(QStringLiteral("enabled"))
                                     ? layer.value(QStringLiteral("enabled")).toBool()
                                     : true;
    if (current_enabled == next_enabled && layer.value(QStringLiteral("solo")).toBool() == next_solo) {
      continue;
    }
    layer.insert(QStringLiteral("enabled"), next_enabled);
    layer.insert(QStringLiteral("solo"), next_solo);
    next_layers[layer_index] = layer;
    changed = true;
  }

  if (!changed) {
    return false;
  }
  setStatus(QStringLiteral("Applied layer visibility state '%1'.").arg(state.value(QStringLiteral("name")).toString()));
  return applyPhotoLayerStackChange(next_layers);
}

bool EditorPreviewController::deletePhotoLayerVisibilityState(int index) {
  if (index < 0 || index >= photo_layer_visibility_states_.size()) {
    return false;
  }
  photo_layer_visibility_states_.removeAt(index);
  setStatus(QStringLiteral("Deleted layer visibility state."));
  emit controlsChanged();
  return true;
}

bool EditorPreviewController::renamePhotoLayer(int index, const QString& name) {
  const auto normalized_name = name.simplified();
  if (index < 0 || index >= photo_layers_.size() || normalized_name.isEmpty()) {
    return false;
  }
  auto next_layers = photo_layers_;
  auto layer = next_layers.at(index).toMap();
  if (layer.value(QStringLiteral("locked")).toBool() ||
      layer.value(QStringLiteral("name")).toString() == normalized_name) {
    return false;
  }
  layer.insert(QStringLiteral("name"), normalized_name);
  next_layers[index] = layer;
  return applyPhotoLayerStackChange(next_layers);
}

bool EditorPreviewController::setPhotoLayerTransform(
    int index,
    int x,
    int y,
    double scale,
    double rotation) {
  if (index < 0 || index >= photo_layers_.size()) {
    return false;
  }
  auto next_layers = photo_layers_;
  auto layer = next_layers.at(index).toMap();
  if (layer.value(QStringLiteral("locked")).toBool()) {
    return false;
  }
  const auto next_x = qBound(-1000000, x, 1000000);
  const auto next_y = qBound(-1000000, y, 1000000);
  const auto next_scale = qBound(0.05, scale, 32.0);
  const auto next_rotation = qBound(-3600.0, rotation, 3600.0);
  if (layer.value(QStringLiteral("x")).toInt() == next_x &&
      layer.value(QStringLiteral("y")).toInt() == next_y &&
      qFuzzyCompare(layer.value(QStringLiteral("scale"), 1.0).toDouble(), next_scale) &&
      qFuzzyCompare(layer.value(QStringLiteral("rotation")).toDouble(), next_rotation)) {
    return false;
  }
  layer.insert(QStringLiteral("x"), next_x);
  layer.insert(QStringLiteral("y"), next_y);
  layer.insert(QStringLiteral("scale"), next_scale);
  layer.insert(QStringLiteral("rotation"), next_rotation);
  next_layers[index] = layer;
  return applyPhotoLayerStackChange(next_layers);
}

QVariantMap EditorPreviewController::photoLayerBounds(int index) const {
  if (index < 0 || index >= photo_layers_.size()) {
    return {};
  }
  return photo_layer_bounds_map(photo_layers_.at(index).toMap());
}

bool EditorPreviewController::snapPhotoLayerToCanvas(
    int index,
    const QString& anchor,
    int canvas_width,
    int canvas_height) {
  if (index < 0 || index >= photo_layers_.size()) {
    return false;
  }
  if (canvas_width <= 0 || canvas_height <= 0) {
    const auto source_size = sourceImageSize();
    canvas_width = source_size.width();
    canvas_height = source_size.height();
  }
  if (canvas_width <= 0 || canvas_height <= 0) {
    return false;
  }

  auto next_layers = photo_layers_;
  auto layer = next_layers.at(index).toMap();
  if (layer.value(QStringLiteral("locked")).toBool() || !photo_layer_has_geometry(layer)) {
    return false;
  }

  const auto bounds = photo_layer_bounds_rect(layer);
  const auto normalized_anchor = anchor.trimmed().toLower();
  auto next_x = layer.value(QStringLiteral("x")).toInt();
  auto next_y = layer.value(QStringLiteral("y")).toInt();
  auto recognized = false;
  const auto left = 0;
  const auto top = 0;
  const auto right = qRound(static_cast<double>(canvas_width) - bounds.width());
  const auto bottom = qRound(static_cast<double>(canvas_height) - bounds.height());
  const auto center_x = qRound((static_cast<double>(canvas_width) - bounds.width()) * 0.5);
  const auto center_y = qRound((static_cast<double>(canvas_height) - bounds.height()) * 0.5);

  if (normalized_anchor.contains(QStringLiteral("left"))) {
    next_x = left;
    recognized = true;
  }
  if (normalized_anchor.contains(QStringLiteral("right"))) {
    next_x = right;
    recognized = true;
  }
  if (normalized_anchor.contains(QStringLiteral("top"))) {
    next_y = top;
    recognized = true;
  }
  if (normalized_anchor.contains(QStringLiteral("bottom"))) {
    next_y = bottom;
    recognized = true;
  }
  if (normalized_anchor == QStringLiteral("center")) {
    next_x = center_x;
    next_y = center_y;
    recognized = true;
  } else {
    if (normalized_anchor == QStringLiteral("horizontal-center") ||
        normalized_anchor == QStringLiteral("center-x")) {
      next_x = center_x;
      recognized = true;
    }
    if (normalized_anchor == QStringLiteral("vertical-center") ||
        normalized_anchor == QStringLiteral("center-y") ||
        normalized_anchor == QStringLiteral("middle")) {
      next_y = center_y;
      recognized = true;
    }
  }
  if (!recognized || (layer.value(QStringLiteral("x")).toInt() == next_x &&
                      layer.value(QStringLiteral("y")).toInt() == next_y)) {
    return false;
  }
  layer.insert(QStringLiteral("x"), next_x);
  layer.insert(QStringLiteral("y"), next_y);
  next_layers[index] = layer;
  return applyPhotoLayerStackChange(next_layers);
}

bool EditorPreviewController::alignPhotoLayers(
    const QVariantList& indices,
    const QString& alignment,
    int canvas_width,
    int canvas_height) {
  auto selected = normalized_photo_layer_indices(indices, photo_layers_.size());
  if (selected.isEmpty()) {
    return false;
  }
  QVector<int> editable;
  editable.reserve(selected.size());
  for (const auto index : selected) {
    const auto layer = photo_layers_.at(index).toMap();
    if (!layer.value(QStringLiteral("locked")).toBool() && photo_layer_has_geometry(layer)) {
      editable.push_back(index);
    }
  }
  if (editable.isEmpty()) {
    return false;
  }

  QRectF reference;
  if (canvas_width <= 0 || canvas_height <= 0) {
    const auto source_size = sourceImageSize();
    canvas_width = source_size.width();
    canvas_height = source_size.height();
  }
  if (canvas_width > 0 && canvas_height > 0) {
    reference = QRectF(0.0, 0.0, static_cast<double>(canvas_width), static_cast<double>(canvas_height));
  } else {
    reference = union_photo_layer_bounds(photo_layers_, editable);
  }
  if (reference.width() <= 0.0 || reference.height() <= 0.0) {
    return false;
  }

  const auto normalized_alignment = alignment.trimmed().toLower();
  auto next_layers = photo_layers_;
  auto changed = false;
  auto recognized = false;
  for (const auto index : editable) {
    auto layer = next_layers.at(index).toMap();
    const auto bounds = photo_layer_bounds_rect(layer);
    auto next_x = layer.value(QStringLiteral("x")).toInt();
    auto next_y = layer.value(QStringLiteral("y")).toInt();
    if (normalized_alignment == QStringLiteral("left")) {
      next_x = qRound(reference.left());
      recognized = true;
    } else if (normalized_alignment == QStringLiteral("right")) {
      next_x = qRound(reference.right() - bounds.width());
      recognized = true;
    } else if (normalized_alignment == QStringLiteral("horizontal-center") ||
               normalized_alignment == QStringLiteral("center-x")) {
      next_x = qRound(reference.center().x() - (bounds.width() * 0.5));
      recognized = true;
    } else if (normalized_alignment == QStringLiteral("top")) {
      next_y = qRound(reference.top());
      recognized = true;
    } else if (normalized_alignment == QStringLiteral("bottom")) {
      next_y = qRound(reference.bottom() - bounds.height());
      recognized = true;
    } else if (normalized_alignment == QStringLiteral("vertical-center") ||
               normalized_alignment == QStringLiteral("center-y") ||
               normalized_alignment == QStringLiteral("middle")) {
      next_y = qRound(reference.center().y() - (bounds.height() * 0.5));
      recognized = true;
    } else if (normalized_alignment == QStringLiteral("center")) {
      next_x = qRound(reference.center().x() - (bounds.width() * 0.5));
      next_y = qRound(reference.center().y() - (bounds.height() * 0.5));
      recognized = true;
    }
    if (layer.value(QStringLiteral("x")).toInt() != next_x ||
        layer.value(QStringLiteral("y")).toInt() != next_y) {
      layer.insert(QStringLiteral("x"), next_x);
      layer.insert(QStringLiteral("y"), next_y);
      next_layers[index] = layer;
      changed = true;
    }
  }
  return recognized && changed && applyPhotoLayerStackChange(next_layers);
}

bool EditorPreviewController::distributePhotoLayers(const QVariantList& indices, const QString& axis) {
  auto selected = normalized_photo_layer_indices(indices, photo_layers_.size());
  QVector<int> editable;
  editable.reserve(selected.size());
  for (const auto index : selected) {
    const auto layer = photo_layers_.at(index).toMap();
    if (!layer.value(QStringLiteral("locked")).toBool() && photo_layer_has_geometry(layer)) {
      editable.push_back(index);
    }
  }
  if (editable.size() < 3) {
    return false;
  }

  const auto normalized_axis = axis.trimmed().toLower();
  const auto horizontal = normalized_axis == QStringLiteral("x") ||
                          normalized_axis == QStringLiteral("horizontal");
  const auto vertical = normalized_axis == QStringLiteral("y") ||
                        normalized_axis == QStringLiteral("vertical");
  if (!horizontal && !vertical) {
    return false;
  }

  std::sort(editable.begin(), editable.end(), [&](int left, int right) {
    const auto left_bounds = photo_layer_bounds_rect(photo_layers_.at(left).toMap());
    const auto right_bounds = photo_layer_bounds_rect(photo_layers_.at(right).toMap());
    return horizontal ? left_bounds.center().x() < right_bounds.center().x()
                      : left_bounds.center().y() < right_bounds.center().y();
  });

  const auto first_bounds = photo_layer_bounds_rect(photo_layers_.at(editable.front()).toMap());
  const auto last_bounds = photo_layer_bounds_rect(photo_layers_.at(editable.back()).toMap());
  const auto first_center = horizontal ? first_bounds.center().x() : first_bounds.center().y();
  const auto last_center = horizontal ? last_bounds.center().x() : last_bounds.center().y();
  const auto step = (last_center - first_center) / static_cast<double>(editable.size() - 1);
  if (std::abs(step) <= 1e-6) {
    return false;
  }

  auto next_layers = photo_layers_;
  auto changed = false;
  for (qsizetype ordinal = 1; ordinal + 1 < editable.size(); ++ordinal) {
    const auto index = editable.at(ordinal);
    auto layer = next_layers.at(index).toMap();
    const auto bounds = photo_layer_bounds_rect(layer);
    const auto target_center = first_center + (step * static_cast<double>(ordinal));
    if (horizontal) {
      const auto next_x = qRound(target_center - (bounds.width() * 0.5));
      if (layer.value(QStringLiteral("x")).toInt() != next_x) {
        layer.insert(QStringLiteral("x"), next_x);
        changed = true;
      }
    } else {
      const auto next_y = qRound(target_center - (bounds.height() * 0.5));
      if (layer.value(QStringLiteral("y")).toInt() != next_y) {
        layer.insert(QStringLiteral("y"), next_y);
        changed = true;
      }
    }
    next_layers[index] = layer;
  }
  return changed && applyPhotoLayerStackChange(next_layers);
}

bool EditorPreviewController::exportPhotoLayer(int index, const QString& file_path) const {
  if (index < 0 || index >= photo_layers_.size()) {
    return false;
  }
  const auto layer = photo_layers_.at(index).toMap();
  if (layer.value(QStringLiteral("kind")).toString().trimmed().toLower() == QStringLiteral("group")) {
    return false;
  }
  auto canvas_size = sourceImageSize();
  if (!canvas_size.isValid()) {
    canvas_size = last_render_output_size_;
  }
  if (!canvas_size.isValid()) {
    canvas_size = QSize(1, 1);
  }
  const auto image = export_image_for_photo_layer(layer, canvas_size);
  return write_export_image(image, file_path);
}

bool EditorPreviewController::exportFlattenedPhoto(const QString& file_path) const {
  if (file_path_.isEmpty()) {
    return false;
  }
  auto state = currentState();
  QImage source_image;
  if (!decode_source_image(file_path_, &source_image, 0, state)) {
    return false;
  }
  auto flattened = apply_crop(source_image, state);
  flattened = render_output_image(
      flattened,
      state,
      output_color_space_for_name(state.output_color_space),
      image_writer_format_for_path(file_path) == "jpg" ? QImage::Format_ARGB32 : QImage::Format_RGBA64,
      nullptr,
      nullptr);
  return write_export_image(std::move(flattened), file_path);
}

QStringList EditorPreviewController::cropAspectPresets() const {
  auto presets = built_in_crop_aspect_presets();
  for (const auto& preset : custom_crop_presets_) {
    if (!presets.contains(preset, Qt::CaseInsensitive)) {
      presets.push_back(preset);
    }
  }
  return presets;
}

QStringList EditorPreviewController::customCropAspectPresets() const {
  return custom_crop_presets_;
}

bool EditorPreviewController::addCustomCropAspectPreset(const QString& preset) {
  const auto normalized = normalized_custom_crop_preset(preset);
  if (normalized.isEmpty() || built_in_crop_aspect_presets().contains(normalized, Qt::CaseInsensitive) ||
      custom_crop_presets_.contains(normalized, Qt::CaseInsensitive)) {
    return false;
  }
  custom_crop_presets_.push_back(normalized);
  saveCustomCropPresets();
  emit controlsChanged();
  return true;
}

bool EditorPreviewController::removeCustomCropAspectPreset(const QString& preset) {
  const auto normalized = normalized_custom_crop_preset(preset);
  if (normalized.isEmpty()) {
    return false;
  }
  for (qsizetype index = 0; index < custom_crop_presets_.size(); ++index) {
    if (custom_crop_presets_.at(index).compare(normalized, Qt::CaseInsensitive) == 0) {
      custom_crop_presets_.removeAt(index);
      if (crop_preset_.compare(normalized, Qt::CaseInsensitive) == 0) {
        setCropPreset(QStringLiteral("Original"));
      }
      saveCustomCropPresets();
      emit controlsChanged();
      return true;
    }
  }
  return false;
}

bool EditorPreviewController::exportCustomCropAspectPresets(const QString& file_path) const {
  const auto normalized_path = normalized_export_file_path(file_path);
  if (normalized_path.isEmpty()) {
    return false;
  }
  const QFileInfo destination_info(normalized_path);
  if (!QDir().mkpath(destination_info.dir().absolutePath())) {
    return false;
  }
  QJsonArray preset_array;
  for (const auto& preset : custom_crop_presets_) {
    preset_array.push_back(preset);
  }
  QJsonObject root;
  root.insert(QStringLiteral("presets"), preset_array);
  QSaveFile file(normalized_path);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  return file.commit();
}

int EditorPreviewController::importCustomCropAspectPresets(const QString& file_path, bool replace) {
  const auto normalized_path = normalized_export_file_path(file_path);
  QFile file(normalized_path);
  if (normalized_path.isEmpty() || !file.exists() || !file.open(QIODevice::ReadOnly)) {
    return 0;
  }
  const auto document = QJsonDocument::fromJson(file.readAll());
  if (!document.isObject()) {
    return 0;
  }
  const auto preset_array = document.object().value(QStringLiteral("presets")).toArray();
  QStringList imported;
  for (const auto& value : preset_array) {
    const auto normalized = normalized_custom_crop_preset(value.toString());
    if (!normalized.isEmpty() &&
        !built_in_crop_aspect_presets().contains(normalized, Qt::CaseInsensitive) &&
        !imported.contains(normalized, Qt::CaseInsensitive)) {
      imported.push_back(normalized);
    }
  }
  if (replace) {
    custom_crop_presets_.clear();
  }
  auto added = 0;
  for (const auto& preset : imported) {
    if (!custom_crop_presets_.contains(preset, Qt::CaseInsensitive)) {
      custom_crop_presets_.push_back(preset);
      ++added;
    }
  }
  if (added > 0 || replace) {
    saveCustomCropPresets();
    emit controlsChanged();
  }
  return added;
}

bool EditorPreviewController::duplicatePhotoLayer(int index) {
  if (index < 0 || index >= photo_layers_.size()) {
    return false;
  }
  const auto layer = photo_layers_.at(index).toMap();
  if (layer.value(QStringLiteral("locked")).toBool()) {
    return false;
  }
  auto duplicate = layer;
  const auto source_id = layer.value(QStringLiteral("id")).toString().isEmpty()
                             ? QStringLiteral("layer")
                             : layer.value(QStringLiteral("id")).toString();
  const auto source_name = layer.value(QStringLiteral("name")).toString().isEmpty()
                               ? QStringLiteral("Layer")
                               : layer.value(QStringLiteral("name")).toString();
  duplicate.insert(
      QStringLiteral("id"),
      QStringLiteral("%1-copy-%2")
          .arg(source_id)
          .arg(QDateTime::currentMSecsSinceEpoch()));
  duplicate.insert(
      QStringLiteral("name"),
      QStringLiteral("%1 Copy").arg(source_name));
  duplicate.insert(QStringLiteral("locked"), false);
  duplicate.insert(QStringLiteral("solo"), false);
  auto next_layers = photo_layers_;
  next_layers.insert(index + 1, duplicate);
  return applyPhotoLayerStackChange(next_layers);
}

bool EditorPreviewController::movePhotoLayer(int index, int offset) {
  if (index < 0 || index >= photo_layers_.size() || offset == 0) {
    return false;
  }
  const auto layer = photo_layers_.at(index).toMap();
  if (layer.value(QStringLiteral("locked")).toBool()) {
    return false;
  }
  const auto target_index = qBound(0, index + offset, static_cast<int>(photo_layers_.size()) - 1);
  if (target_index == index) {
    return false;
  }
  auto next_layers = photo_layers_;
  const auto value = next_layers.takeAt(index);
  next_layers.insert(target_index, value);
  return applyPhotoLayerStackChange(next_layers);
}

bool EditorPreviewController::deletePhotoLayer(int index) {
  if (index < 0 || index >= photo_layers_.size()) {
    return false;
  }
  const auto layer = photo_layers_.at(index).toMap();
  if (layer.value(QStringLiteral("locked")).toBool()) {
    return false;
  }
  auto next_layers = photo_layers_;
  next_layers.removeAt(index);
  return applyPhotoLayerStackChange(next_layers);
}

void EditorPreviewController::addMask(const QString& kind) {
  auto next_stack = mask_stack_;
  auto mask = default_mask_definition(kind, static_cast<int>(next_stack.size()) + 1);
  if (hasActiveMask()) {
    const auto active_mask = activeMaskDefinition();
    if (is_selection_mask(active_mask)) {
      inherit_selection_mask_set(&mask, active_mask);
    }
  }
  next_stack.append(mask);
  applyMaskStackChange(next_stack, static_cast<int>(next_stack.size()) - 1);
}

void EditorPreviewController::addAIMask(const QString& semantic_kind) {
  const auto kind = normalized_mask_kind(semantic_kind);
  if (!is_ai_segmentation_mask_kind(kind)) {
    setStatus(QStringLiteral("Choose a supported AI mask target."));
    return;
  }
  const auto label = ai_segmentation_mask_label(kind);
  if (file_path_.isEmpty()) {
    setStatus(QStringLiteral("Load an image before creating an AI %1 mask.").arg(label.toLower()));
    return;
  }

  QImage source_image;
  if (!decodeSourceImage(&source_image, 1024)) {
    qWarning() << "addAIMask: failed to decode source image";
    setStatus(QStringLiteral("Unable to decode the source image for AI masking."));
    return;
  }

  const auto result = generate_semantic_mask(source_image, kind);
  if (!result.success) {
    qWarning() << "addAIMask:" << kind << result.error_message;
    setStatus(
        result.error_message.isEmpty()
            ? QStringLiteral("AI %1 masking is unavailable.").arg(label.toLower())
            : result.error_message);
    return;
  }

  auto next_stack = mask_stack_;
  auto mask_def = default_mask_definition(kind, static_cast<int>(next_stack.size()) + 1);
  if (hasActiveMask()) {
    const auto active_mask = activeMaskDefinition();
    if (is_selection_mask(active_mask)) {
      inherit_selection_mask_set(&mask_def, active_mask);
    }
  }
  if (kind == QStringLiteral("hair") &&
      map_double_value(mask_def, QStringLiteral("selectionFineEdge"), 0.0) <= 1e-6) {
    mask_def.insert(QStringLiteral("selectionFineEdge"), 65.0);
  }
  mask_def.insert(QStringLiteral("feather"), 0.5);
  mask_def.insert(QStringLiteral("segmentationData"), result.mask_data);
  mask_def.insert(QStringLiteral("segmentationWidth"), result.width);
  mask_def.insert(QStringLiteral("segmentationHeight"), result.height);
  next_stack.append(mask_def);
  applyMaskStackChange(next_stack, static_cast<int>(next_stack.size()) - 1);
  setStatus(QStringLiteral("AI %1 mask added.").arg(label.toLower()));
}

void EditorPreviewController::addSubjectMask() {
  addAIMask(QStringLiteral("subject"));
}

bool EditorPreviewController::addAlphaImageMask(const QString& file_path) {
  const auto normalized_path = local_file_path_from_user_input(file_path);
  if (normalized_path.isEmpty()) {
    setStatus(QStringLiteral("Choose an alpha mask image to import."));
    return false;
  }
  if (!QFileInfo::exists(normalized_path)) {
    setStatus(QStringLiteral("Alpha mask image was not found."));
    return false;
  }

  const auto result = alpha_mask_from_image_file(normalized_path);
  if (!result.success) {
    setStatus(
        result.error_message.isEmpty()
            ? QStringLiteral("Unable to import alpha mask image.")
            : result.error_message);
    return false;
  }

  auto next_stack = mask_stack_;
  auto mask_def = default_mask_definition(QStringLiteral("alpha"), static_cast<int>(next_stack.size()) + 1);
  if (hasActiveMask()) {
    const auto active_mask = activeMaskDefinition();
    if (is_selection_mask(active_mask)) {
      inherit_selection_mask_set(&mask_def, active_mask);
    }
  }
  const auto base_name = QFileInfo(normalized_path).completeBaseName().simplified();
  if (!base_name.isEmpty()) {
    mask_def.insert(QStringLiteral("name"), QStringLiteral("Alpha %1").arg(base_name));
  }
  mask_def.insert(QStringLiteral("alphaSourcePath"), normalized_path);
  mask_def.insert(QStringLiteral("feather"), 0.5);
  mask_def.insert(QStringLiteral("segmentationData"), result.mask_data);
  mask_def.insert(QStringLiteral("segmentationWidth"), result.width);
  mask_def.insert(QStringLiteral("segmentationHeight"), result.height);
  next_stack.append(mask_def);
  applyMaskStackChange(next_stack, static_cast<int>(next_stack.size()) - 1);
  setStatus(QStringLiteral("Alpha image mask added."));
  return true;
}

void EditorPreviewController::addObjectMaskAt(double normalized_x, double normalized_y) {
  pickMaskAt(normalized_x, normalized_y, QStringLiteral("selection"), true);
}

QString EditorPreviewController::pickMaskAt(
    double normalized_x,
    double normalized_y,
    const QString& intent,
    bool create_new) {
  QVariantList points;
  QVariantMap point;
  point.insert(QStringLiteral("x"), qBound(0.0, normalized_x, 1.0));
  point.insert(QStringLiteral("y"), qBound(0.0, normalized_y, 1.0));
  points.append(point);
  return pickMaskPath(points, intent, create_new);
}

QString EditorPreviewController::pickMaskPath(
    const QVariantList& points,
    const QString& intent,
    bool create_new) {
  if (file_path_.isEmpty()) {
    setStatus(QStringLiteral("Load an image before picking a mask target."));
    return QStringLiteral("error");
  }

  QImage source_image;
  if (!decodeSourceImage(&source_image, 1024)) {
    qWarning() << "pickMaskPath: failed to decode source image";
    setStatus(QStringLiteral("Unable to decode the source image for smart selection."));
    return QStringLiteral("error");
  }

  const auto pick_intent = normalized_pick_intent(intent);
  const auto normalized_points = normalized_smart_select_path(points);
  const auto prefer_single_candidate = normalized_points.size() == 1;

  if (vision_segmentation_available() && ensureSmartSelectInstanceMap()) {
    const auto candidate_instance_ids = smartSelectCandidateInstanceIdsForPath(points);
    const auto preview_instance_ids =
        prefer_single_candidate
            ? single_smart_select_candidate_instance_ids(
                  candidate_instance_ids,
                  candidate_instance_ids == smart_select_preview_candidate_instance_ids_
                      ? smart_select_preview_candidate_index_
                      : 0)
            : candidate_instance_ids;
    auto preview_mask = smartSelectPreviewMaskForInstances(preview_instance_ids);
    if (!preview_mask.isEmpty()) {
      updateSmartSelectPreviewState(
          preview_mask,
          preview_instance_ids,
          prefer_single_candidate ? candidate_instance_ids : QVector<int>{});

      auto result = generate_object_mask_for_instances(source_image, preview_instance_ids);
      if (!result.success) {
        result = segmentation_result_from_mask_variant(preview_mask);
      }
      if (normalized_points.size() == 1) {
        const auto point = normalized_points.constFirst();
        const auto prompt_result =
            compute_single_point_object_pick_result(source_image, point.x(), point.y());
        if (should_prefer_prompt_object_pick_result(
                result,
                prompt_result,
                point.x(),
                point.y())) {
          result = prompt_result;
          updateSmartSelectPreviewResult(prompt_result);
        }
      }

      if (result.success) {
        const auto selection_operation = pick_intent_mask_operation(pick_intent);
        return pick_intent == QStringLiteral("remove")
                   ? pickObjectRemoveMask(result, create_new)
                   : pickObjectSelectionMask(result, selection_operation, create_new);
      }
    }
  }

  const auto result = compute_smart_select_result(source_image, points);
  if (!result.success) {
    resetSmartSelectPreview();
    setStatus(
        pick_intent == QStringLiteral("remove")
            ? QStringLiteral("AI could not isolate that target. Drag across the target again or choose Remove Brush explicitly.")
            : QStringLiteral("AI could not isolate that target. Drag across the object again or refine with Brush Add/Erase."));
    return pick_intent == QStringLiteral("remove")
               ? QStringLiteral("remove-missed")
               : QStringLiteral("selection-missed");
  }

  updateSmartSelectPreviewResult(result);
  const auto selection_operation = pick_intent_mask_operation(pick_intent);
  return pick_intent == QStringLiteral("remove")
             ? pickObjectRemoveMask(result, create_new)
             : pickObjectSelectionMask(result, selection_operation, create_new);
}

void EditorPreviewController::updateSmartSelectPreview(
    double normalized_x,
    double normalized_y) {
  QVariantList points;
  QVariantMap point;
  point.insert(QStringLiteral("x"), qBound(0.0, normalized_x, 1.0));
  point.insert(QStringLiteral("y"), qBound(0.0, normalized_y, 1.0));
  points.append(point);
  updateSmartSelectPreviewPath(points);
}

void EditorPreviewController::scheduleSmartSelectPreview(
    double normalized_x,
    double normalized_y) {
  QVariantList points;
  QVariantMap point;
  point.insert(QStringLiteral("x"), qBound(0.0, normalized_x, 1.0));
  point.insert(QStringLiteral("y"), qBound(0.0, normalized_y, 1.0));
  points.append(point);
  scheduleSmartSelectPreviewPath(points);
}

void EditorPreviewController::scheduleSmartSelectPreviewPath(const QVariantList& points) {
  const auto quantized_points = quantized_smart_select_preview_path(points);
  if (file_path_.isEmpty() || quantized_points.isEmpty()) {
    resetSmartSelectPreview();
    return;
  }

  const auto state = currentState();
  if (smart_select_preview_request_pending_ &&
      pending_smart_select_preview_request_.file_path == file_path_ &&
      pending_smart_select_preview_request_.points == quantized_points &&
      pending_smart_select_preview_request_.state == state) {
    return;
  }

  const auto generation = ++smart_select_preview_generation_;
  if (vision_segmentation_available() && ensureSmartSelectInstanceMap()) {
    const auto candidate_instance_ids = smartSelectCandidateInstanceIdsForPath(quantized_points);
    const auto preview_instance_ids =
        quantized_points.size() == 1
            ? single_smart_select_candidate_instance_ids(
                  candidate_instance_ids,
                  candidate_instance_ids == smart_select_preview_candidate_instance_ids_
                      ? smart_select_preview_candidate_index_
                      : 0)
            : candidate_instance_ids;
    const auto preview_mask = smartSelectPreviewMaskForInstances(preview_instance_ids);
    if (!preview_mask.isEmpty()) {
      smart_select_preview_timer_.stop();
      smart_select_preview_request_pending_ = false;
      updateSmartSelectPreviewState(
          preview_mask,
          preview_instance_ids,
          quantized_points.size() == 1 ? candidate_instance_ids : QVector<int>{});
      return;
    }
  }

  pending_smart_select_preview_request_ = SmartSelectPreviewRequest{
      .file_path = file_path_,
      .state = state,
      .points = quantized_points,
      .generation = generation,
  };
  smart_select_preview_request_pending_ = true;
  smart_select_preview_timer_.start();
}

void EditorPreviewController::updateSmartSelectPreviewPath(const QVariantList& points) {
  ++smart_select_preview_generation_;
  smart_select_preview_timer_.stop();
  smart_select_preview_request_pending_ = false;
  if (vision_segmentation_available() && ensureSmartSelectInstanceMap()) {
    const auto candidate_instance_ids = smartSelectCandidateInstanceIdsForPath(points);
    const auto normalized_points = normalized_smart_select_path(points);
    const auto preview_instance_ids =
        normalized_points.size() == 1
            ? single_smart_select_candidate_instance_ids(
                  candidate_instance_ids,
                  candidate_instance_ids == smart_select_preview_candidate_instance_ids_
                      ? smart_select_preview_candidate_index_
                      : 0)
            : candidate_instance_ids;
    const auto preview_mask = smartSelectPreviewMaskForInstances(preview_instance_ids);
    if (!preview_mask.isEmpty()) {
      updateSmartSelectPreviewState(
          preview_mask,
          preview_instance_ids,
          normalized_points.size() == 1 ? candidate_instance_ids : QVector<int>{});
      return;
    }
  }

  QImage source_image;
  if (!decodeSourceImage(&source_image, 1024)) {
    resetSmartSelectPreview();
    return;
  }

  const auto result = compute_smart_select_result(source_image, points);
  if (!result.success) {
    resetSmartSelectPreview();
    return;
  }
  updateSmartSelectPreviewResult(result);
}

void EditorPreviewController::clearSmartSelectPreview() {
  resetSmartSelectPreview();
}

void EditorPreviewController::cycleSmartSelectCandidate(int delta) {
  if (smart_select_preview_candidate_instance_ids_.size() <= 1 || delta == 0) {
    return;
  }

  const auto candidate_count =
      static_cast<int>(smart_select_preview_candidate_instance_ids_.size());
  auto next_index = smart_select_preview_candidate_index_;
  next_index = ((next_index + delta) % candidate_count + candidate_count) % candidate_count;
  if (next_index == smart_select_preview_candidate_index_) {
    return;
  }

  const auto next_instance_ids = single_smart_select_candidate_instance_ids(
      smart_select_preview_candidate_instance_ids_,
      next_index);
  const auto next_preview_mask = smartSelectPreviewMaskForInstances(next_instance_ids);
  if (next_preview_mask.isEmpty()) {
    return;
  }

  smart_select_preview_candidate_index_ = next_index;
  updateSmartSelectPreviewState(
      next_preview_mask,
      next_instance_ids,
      smart_select_preview_candidate_instance_ids_);
  setStatus(
      QStringLiteral("Smart select candidate %1 of %2.")
          .arg(next_index + 1)
          .arg(candidate_count));
}

void EditorPreviewController::deselectActiveMask() {
  setActiveMaskIndex(-1);
}

void EditorPreviewController::smartSelectAddAt(
    double normalized_x,
    double normalized_y,
    bool create_new) {
  pickMaskAt(normalized_x, normalized_y, QStringLiteral("selection"), create_new);
}

void EditorPreviewController::smartSelectSubtractAt(
    double normalized_x,
    double normalized_y,
    bool create_new) {
  pickMaskAt(normalized_x, normalized_y, QStringLiteral("selection-subtract"), create_new);
}

void EditorPreviewController::smartSelectObjectAt(
    double normalized_x,
    double normalized_y,
    bool create_new) {
  smartSelectAddAt(normalized_x, normalized_y, create_new);
}

void EditorPreviewController::objectRemoveAt(
    double normalized_x,
    double normalized_y,
    bool create_new) {
  pickMaskAt(normalized_x, normalized_y, QStringLiteral("remove"), create_new);
}

QString EditorPreviewController::pickObjectSelectionMask(
    const SegmentationResult& result,
    const QString& operation,
    bool create_new) {
  if (!result.success || !valid_segmentation_payload(result.mask_data, result.width, result.height)) {
    setStatus(QStringLiteral("Smart selection could not isolate that target."));
    return QStringLiteral("error");
  }

  auto next_stack = mask_stack_;
  const auto normalized_operation = normalized_mask_operation(operation);
  const auto can_extend_active =
      !create_new && hasActiveMask() && activeMaskKind() == QStringLiteral("object") &&
      activeMaskToolRole() == QStringLiteral("selection") &&
      activeMaskOperation() == normalized_operation;

  if (can_extend_active) {
    auto mask_def = activeMaskDefinition();
    mask_def.insert(
        QStringLiteral("segmentationData"),
        union_segmentation_masks(
            mask_def.value(QStringLiteral("segmentationData")).toByteArray(),
            mask_def.value(QStringLiteral("segmentationWidth")).toInt(),
            mask_def.value(QStringLiteral("segmentationHeight")).toInt(),
            result.mask_data,
            result.width,
            result.height));
    mask_def.insert(QStringLiteral("segmentationWidth"), result.width);
    mask_def.insert(QStringLiteral("segmentationHeight"), result.height);
    if (mask_def.value(QStringLiteral("name")).toString().trimmed().isEmpty()) {
      mask_def.insert(
          QStringLiteral("name"),
          normalized_operation == QStringLiteral("subtract")
              ? QStringLiteral("Subtract Selection")
              : QStringLiteral("Smart Selection"));
    }
    updateActiveMaskDefinition(mask_def);
    setStatus(
        normalized_operation == QStringLiteral("subtract")
            ? QStringLiteral("Selection area subtracted from the current smart selection.")
            : QStringLiteral("Selection area added to the current smart selection."));
    return QStringLiteral("selection-ai");
  }

  auto mask_def = default_mask_definition(QStringLiteral("object"), static_cast<int>(next_stack.size()) + 1);
  mask_def.insert(
      QStringLiteral("name"),
      normalized_operation == QStringLiteral("subtract")
          ? QStringLiteral("Subtract Selection %1").arg(next_stack.size() + 1)
          : QStringLiteral("Smart Selection %1").arg(next_stack.size() + 1));
  mask_def.insert(QStringLiteral("toolRole"), QStringLiteral("selection"));
  mask_def.insert(QStringLiteral("operation"), normalized_operation);
  mask_def.insert(QStringLiteral("committed"), true);
  if (!create_new && hasActiveMask()) {
    const auto active_mask = activeMaskDefinition();
    if (is_selection_mask(active_mask)) {
      inherit_selection_mask_set(&mask_def, active_mask);
    }
  }
  mask_def.insert(QStringLiteral("feather"), 0.35);
  mask_def.insert(QStringLiteral("segmentationData"), result.mask_data);
  mask_def.insert(QStringLiteral("segmentationWidth"), result.width);
  mask_def.insert(QStringLiteral("segmentationHeight"), result.height);
  next_stack.append(mask_def);

  applyMaskStackChange(next_stack, static_cast<int>(next_stack.size()) - 1);
  setStatus(
      normalized_operation == QStringLiteral("subtract")
          ? QStringLiteral("Selection area subtracted from the current smart selection.")
          : QStringLiteral("Smart selection created."));
  return QStringLiteral("selection-ai");
}

QString EditorPreviewController::pickObjectRemoveMask(
    const SegmentationResult& result,
    bool create_new) {
  if (!result.success || !valid_segmentation_payload(result.mask_data, result.width, result.height)) {
    setStatus(QStringLiteral("Smart selection could not isolate that target."));
    return QStringLiteral("error");
  }

  auto next_stack = mask_stack_;
  auto target_index = -1;
  if (!create_new) {
    if (hasActiveMask()) {
      const auto active_mask = activeMaskDefinition();
      if (active_mask.value(QStringLiteral("kind")).toString() == QStringLiteral("object") &&
          active_mask.value(QStringLiteral("toolRole")).toString() == QStringLiteral("retouch") &&
          active_mask.value(QStringLiteral("retouchMode")).toString().trimmed().toLower() ==
              QStringLiteral("remove") &&
          !active_mask.value(QStringLiteral("committed")).toBool()) {
        target_index = active_mask_index_;
      }
    }
    if (target_index < 0) {
      target_index = findPendingRetouchMaskIndex(
          QStringLiteral("remove"),
          QStringLiteral("object"));
    }
  }

  if (target_index >= 0) {
    auto mask_def = next_stack.at(target_index).toMap();
    mask_def.insert(
        QStringLiteral("segmentationData"),
        union_segmentation_masks(
            mask_def.value(QStringLiteral("segmentationData")).toByteArray(),
            mask_def.value(QStringLiteral("segmentationWidth")).toInt(),
            mask_def.value(QStringLiteral("segmentationHeight")).toInt(),
            result.mask_data,
            result.width,
            result.height));
    mask_def.insert(QStringLiteral("segmentationWidth"), result.width);
    mask_def.insert(QStringLiteral("segmentationHeight"), result.height);
    if (mask_def.value(QStringLiteral("name")).toString().trimmed().isEmpty()) {
      mask_def.insert(QStringLiteral("name"), QStringLiteral("Object Remove"));
    }
    next_stack[target_index] = normalized_mask_definition(mask_def, target_index + 1);
    applyMaskStackChange(next_stack, target_index, false);
    setStatus(QStringLiteral("Object remove area expanded. Apply when ready."));
    return QStringLiteral("remove-ai");
  }

  auto mask_def = default_mask_definition(QStringLiteral("object"), static_cast<int>(next_stack.size()) + 1);
  mask_def.insert(
      QStringLiteral("name"),
      QStringLiteral("Remove Target %1").arg(next_stack.size() + 1));
  mask_def.insert(QStringLiteral("toolRole"), QStringLiteral("retouch"));
  mask_def.insert(QStringLiteral("retouchMode"), QStringLiteral("remove"));
  mask_def.insert(QStringLiteral("committed"), false);
  mask_def.insert(QStringLiteral("removeStrength"), 100.0);
  mask_def.insert(QStringLiteral("feather"), 0.35);
  mask_def.insert(QStringLiteral("segmentationData"), result.mask_data);
  mask_def.insert(QStringLiteral("segmentationWidth"), result.width);
  mask_def.insert(QStringLiteral("segmentationHeight"), result.height);
  next_stack.append(mask_def);

  applyMaskStackChange(next_stack, static_cast<int>(next_stack.size()) - 1, false);
  setStatus(QStringLiteral("Object remove staged. Paint to refine, then apply."));
  return QStringLiteral("remove-ai");
}

void EditorPreviewController::startSelectionBrush(bool subtract) {
  auto next_stack = mask_stack_;
  const auto insert_index =
      hasActiveMask() ? qBound(0, active_mask_index_ + 1, static_cast<int>(next_stack.size())) : static_cast<int>(next_stack.size());

  auto mask_def = buildBrushMask(
      subtract ? QStringLiteral("Erase Brush %1").arg(insert_index + 1)
               : QStringLiteral("Select Brush %1").arg(insert_index + 1),
      subtract ? QStringLiteral("subtract") : QStringLiteral("add"),
      QStringLiteral("selection"),
      QString(),
      true,
      insert_index + 1);
  if (hasActiveMask()) {
    const auto active_mask = activeMaskDefinition();
    if (is_selection_mask(active_mask)) {
      inherit_selection_mask_set(&mask_def, active_mask);
    }
  }
  mask_def.insert(QStringLiteral("brushPreset"), QStringLiteral("Selection Brush"));
  mask_def.insert(QStringLiteral("brushSize"), 0.06);
  mask_def.insert(QStringLiteral("brushFlow"), 1.0);
  mask_def.insert(QStringLiteral("brushHardness"), 0.45);
  mask_def.insert(QStringLiteral("brushSpacing"), 0.75);
  mask_def.insert(QStringLiteral("brushSmoothing"), 0.18);
  mask_def.insert(QStringLiteral("brushBuildup"), true);
  mask_def.insert(QStringLiteral("brushEraseMode"), false);
  mask_def.insert(QStringLiteral("brushAutoMask"), false);
  mask_def.insert(QStringLiteral("brushAutoMaskTolerance"), 0.18);
  mask_def.insert(QStringLiteral("feather"), 0.55);
  next_stack.insert(insert_index, mask_def);
  applyMaskStackChange(next_stack, insert_index);
  setStatus(
      subtract ? QStringLiteral("Erase Brush ready. Paint directly on the image to remove areas from the selection.")
                : QStringLiteral("Select Brush ready. Paint directly on the image to add areas to the selection."));
}

void EditorPreviewController::startAIMaskRefineBrush(bool subtract) {
  if (!hasActiveMask() || !is_ai_segmentation_mask_kind(activeMaskKind())) {
    setStatus(QStringLiteral("Select an AI mask before refining with the AI brush."));
    return;
  }
  startSelectionEdgeBrush(subtract);
}

void EditorPreviewController::startSelectionEdgeBrush(bool subtract) {
  auto next_stack = mask_stack_;
  const auto insert_index =
      hasActiveMask() ? qBound(0, active_mask_index_ + 1, static_cast<int>(next_stack.size())) : static_cast<int>(next_stack.size());

  auto mask_def = buildBrushMask(
      subtract ? QStringLiteral("Edge Erase %1").arg(insert_index + 1)
               : QStringLiteral("Edge Add %1").arg(insert_index + 1),
      subtract ? QStringLiteral("subtract") : QStringLiteral("add"),
      QStringLiteral("selection"),
      QString(),
      true,
      insert_index + 1);
  if (hasActiveMask()) {
    const auto active_mask = activeMaskDefinition();
    if (is_selection_mask(active_mask)) {
      inherit_selection_mask_set(&mask_def, active_mask);
    }
  }
  mask_def.insert(QStringLiteral("brushPreset"), QStringLiteral("Edge Aware"));
  mask_def.insert(QStringLiteral("brushSize"), 0.026);
  mask_def.insert(QStringLiteral("brushFlow"), 1.0);
  mask_def.insert(QStringLiteral("brushHardness"), 0.66);
  mask_def.insert(QStringLiteral("brushSpacing"), 0.42);
  mask_def.insert(QStringLiteral("brushSmoothing"), 0.34);
  mask_def.insert(QStringLiteral("brushBuildup"), true);
  mask_def.insert(QStringLiteral("brushEraseMode"), false);
  mask_def.insert(QStringLiteral("brushAutoMask"), true);
  mask_def.insert(QStringLiteral("brushAutoMaskTolerance"), 0.14);
  mask_def.insert(QStringLiteral("feather"), 0.34);
  next_stack.insert(insert_index, mask_def);
  applyMaskStackChange(next_stack, insert_index);
  setStatus(
      subtract ? QStringLiteral("Edge Erase ready. Paint directly along the boundary to trim the selection.")
               : QStringLiteral("Edge Add ready. Paint directly along the boundary to recover missed edges."));
}

void EditorPreviewController::startRemoveBrush() {
  const auto reusable_index = findReusableRetouchBrushIndex(QStringLiteral("remove"));
  if (reusable_index >= 0) {
    setActiveMaskIndex(reusable_index);
    setStatus(QStringLiteral("Remove Brush ready. Paint an opaque highlight over anything you want removed, then hit Remove."));
    return;
  }

  auto next_stack = mask_stack_;
  const auto insert_index =
      hasActiveMask() ? qBound(0, active_mask_index_ + 1, static_cast<int>(next_stack.size())) : static_cast<int>(next_stack.size());

  auto mask_def = buildBrushMask(
      QStringLiteral("Remove Brush %1").arg(insert_index + 1),
      QStringLiteral("add"),
      QStringLiteral("retouch"),
      QStringLiteral("remove"),
      false,
      insert_index + 1);
  mask_def.insert(QStringLiteral("removeStrength"), 100.0);
  mask_def.insert(QStringLiteral("brushPreset"), QStringLiteral("Retouch Soft"));
  mask_def.insert(QStringLiteral("brushSize"), 0.08);
  mask_def.insert(QStringLiteral("brushFlow"), 0.92);
  mask_def.insert(QStringLiteral("brushHardness"), 0.14);
  mask_def.insert(QStringLiteral("brushSpacing"), 0.55);
  mask_def.insert(QStringLiteral("brushSmoothing"), 0.12);
  mask_def.insert(QStringLiteral("brushBuildup"), true);
  mask_def.insert(QStringLiteral("brushEraseMode"), false);
  mask_def.insert(QStringLiteral("brushAutoMask"), false);
  mask_def.insert(QStringLiteral("brushAutoMaskTolerance"), 0.18);
  mask_def.insert(QStringLiteral("feather"), 0.86);
  next_stack.insert(insert_index, mask_def);
  applyMaskStackChange(next_stack, insert_index, false);
  setStatus(QStringLiteral("Remove Brush ready. Paint an opaque highlight over anything you want removed, then hit Remove."));
}

void EditorPreviewController::startHealBrush() {
  const auto reusable_index = findReusableRetouchBrushIndex(QStringLiteral("heal"));
  if (reusable_index >= 0) {
    setActiveMaskIndex(reusable_index);
    setStatus(QStringLiteral("Heal Brush ready. Paint to refine the staged retouch, then apply."));
    return;
  }

  auto next_stack = mask_stack_;
  const auto insert_index =
      hasActiveMask() ? qBound(0, active_mask_index_ + 1, static_cast<int>(next_stack.size()))
                      : static_cast<int>(next_stack.size());

  auto mask_def = buildBrushMask(
      QStringLiteral("Heal Brush %1").arg(insert_index + 1),
      QStringLiteral("add"),
      QStringLiteral("retouch"),
      QStringLiteral("heal"),
      false,
      insert_index + 1);
  mask_def.insert(QStringLiteral("removeStrength"), 82.0);
  mask_def.insert(QStringLiteral("brushPreset"), QStringLiteral("Heal Detail"));
  mask_def.insert(QStringLiteral("brushSize"), 0.07);
  mask_def.insert(QStringLiteral("brushFlow"), 0.78);
  mask_def.insert(QStringLiteral("brushHardness"), 0.20);
  mask_def.insert(QStringLiteral("brushSpacing"), 0.45);
  mask_def.insert(QStringLiteral("brushSmoothing"), 0.22);
  mask_def.insert(QStringLiteral("brushBuildup"), true);
  mask_def.insert(QStringLiteral("brushEraseMode"), false);
  mask_def.insert(QStringLiteral("brushAutoMask"), true);
  mask_def.insert(QStringLiteral("brushAutoMaskTolerance"), 0.16);
  mask_def.insert(QStringLiteral("feather"), 0.80);
  next_stack.insert(insert_index, mask_def);
  applyMaskStackChange(next_stack, insert_index, false);
  setStatus(QStringLiteral("Heal Brush ready. Paint over blemishes and distractions, then apply."));
}

void EditorPreviewController::startPatchBrush() {
  const auto reusable_index = findReusableRetouchBrushIndex(QStringLiteral("patch"));
  if (reusable_index >= 0) {
    setActiveMaskIndex(reusable_index);
    setStatus(QStringLiteral("Patch Brush ready. Paint the repair area, then apply."));
    return;
  }

  auto next_stack = mask_stack_;
  const auto insert_index =
      hasActiveMask() ? qBound(0, active_mask_index_ + 1, static_cast<int>(next_stack.size()))
                      : static_cast<int>(next_stack.size());

  auto mask_def = buildBrushMask(
      QStringLiteral("Patch Brush %1").arg(insert_index + 1),
      QStringLiteral("add"),
      QStringLiteral("retouch"),
      QStringLiteral("patch"),
      false,
      insert_index + 1);
  mask_def.insert(QStringLiteral("removeStrength"), 88.0);
  mask_def.insert(QStringLiteral("brushPreset"), QStringLiteral("Retouch Soft"));
  mask_def.insert(QStringLiteral("brushSize"), 0.10);
  mask_def.insert(QStringLiteral("brushFlow"), 0.86);
  mask_def.insert(QStringLiteral("brushHardness"), 0.12);
  mask_def.insert(QStringLiteral("brushSpacing"), 0.50);
  mask_def.insert(QStringLiteral("brushSmoothing"), 0.18);
  mask_def.insert(QStringLiteral("brushBuildup"), true);
  mask_def.insert(QStringLiteral("brushEraseMode"), false);
  mask_def.insert(QStringLiteral("brushAutoMask"), true);
  mask_def.insert(QStringLiteral("brushAutoMaskTolerance"), 0.20);
  mask_def.insert(QStringLiteral("feather"), 0.88);
  next_stack.insert(insert_index, mask_def);
  applyMaskStackChange(next_stack, insert_index, false);
  setStatus(QStringLiteral("Patch Brush ready. Paint the repair area, then apply."));
}

void EditorPreviewController::removeDustSpots() {
  QImage source_image;
  if (!decodeSourceImage(&source_image, 1024)) {
    setStatus(QStringLiteral("Dust removal needs a decoded source image."));
    return;
  }

  const auto spots = detect_dust_spots(source_image, 64);
  if (spots.empty()) {
    setStatus(QStringLiteral("Dust removal found no small dust spots in smooth areas."));
    return;
  }

  const auto mask_data = dust_spot_mask_data(source_image.width(), source_image.height(), spots);
  if (!valid_segmentation_payload(mask_data, source_image.width(), source_image.height())) {
    setStatus(QStringLiteral("Dust removal could not build a valid retouch mask."));
    return;
  }

  auto next_stack = mask_stack_;
  const auto insert_index =
      hasActiveMask() ? qBound(0, active_mask_index_ + 1, static_cast<int>(next_stack.size()))
                      : static_cast<int>(next_stack.size());
  auto mask_def = default_mask_definition(QStringLiteral("object"), insert_index + 1);
  mask_def.insert(QStringLiteral("name"), QStringLiteral("Dust Removal %1").arg(insert_index + 1));
  mask_def.insert(QStringLiteral("operation"), QStringLiteral("add"));
  mask_def.insert(QStringLiteral("toolRole"), QStringLiteral("retouch"));
  mask_def.insert(QStringLiteral("retouchMode"), QStringLiteral("dust"));
  mask_def.insert(QStringLiteral("committed"), false);
  mask_def.insert(QStringLiteral("removeStrength"), 74.0);
  mask_def.insert(QStringLiteral("feather"), 0.72);
  mask_def.insert(QStringLiteral("segmentationData"), mask_data);
  mask_def.insert(QStringLiteral("segmentationWidth"), source_image.width());
  mask_def.insert(QStringLiteral("segmentationHeight"), source_image.height());
  next_stack.insert(insert_index, mask_def);
  applyMaskStackChange(next_stack, insert_index, true);
  setStatus(QStringLiteral("Dust removal staged for %1 spot%2. Review, then apply or discard.")
                .arg(spots.size())
                .arg(spots.size() == 1 ? QString() : QStringLiteral("s")));
}

void EditorPreviewController::saveActiveDustMap() {
  if (!hasActiveMask()) {
    setStatus(QStringLiteral("Select a dust removal mask before saving a sensor dust map."));
    return;
  }

  const auto mask = activeMaskDefinition();
  if (mask.value(QStringLiteral("toolRole")).toString() != QStringLiteral("retouch") ||
      mask.value(QStringLiteral("retouchMode")).toString().trimmed().toLower() != QStringLiteral("dust")) {
    setStatus(QStringLiteral("Sensor dust maps can only be saved from a dust removal mask."));
    return;
  }

  const auto segmentation_data = mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto width = mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto height = mask.value(QStringLiteral("segmentationHeight")).toInt();
  if (!valid_segmentation_payload(segmentation_data, width, height)) {
    setStatus(QStringLiteral("The active dust mask has no reusable matte to save."));
    return;
  }

  if (!write_sensor_dust_map(
          camera_label_,
          lens_model_,
          segmentation_data,
          width,
          height,
          map_double_value(mask, QStringLiteral("removeStrength"), 74.0),
          map_double_value(mask, QStringLiteral("feather"), 0.72))) {
    setStatus(QStringLiteral("Sensor dust map could not be saved."));
    return;
  }

  emit controlsChanged();
  setStatus(QStringLiteral("Sensor dust map saved for this camera and lens."));
}

void EditorPreviewController::applySensorDustMap() {
  const auto payload = read_sensor_dust_map(camera_label_, lens_model_);
  if (!payload.valid) {
    setStatus(QStringLiteral("No sensor dust map is saved for this camera and lens."));
    return;
  }

  QImage source_image;
  if (!decodeSourceImage(&source_image, 1024)) {
    setStatus(QStringLiteral("Sensor dust map needs a decoded source image."));
    return;
  }

  const auto mask_data = resample_segmentation_mask(
      payload.segmentation_data,
      payload.width,
      payload.height,
      source_image.width(),
      source_image.height());
  if (!valid_segmentation_payload(mask_data, source_image.width(), source_image.height())) {
    setStatus(QStringLiteral("Saved sensor dust map could not be scaled to this image."));
    return;
  }

  auto next_stack = mask_stack_;
  const auto insert_index =
      hasActiveMask() ? qBound(0, active_mask_index_ + 1, static_cast<int>(next_stack.size()))
                      : static_cast<int>(next_stack.size());
  auto mask_def = default_mask_definition(QStringLiteral("object"), insert_index + 1);
  mask_def.insert(QStringLiteral("name"), QStringLiteral("Sensor Dust Map %1").arg(insert_index + 1));
  mask_def.insert(QStringLiteral("operation"), QStringLiteral("add"));
  mask_def.insert(QStringLiteral("toolRole"), QStringLiteral("retouch"));
  mask_def.insert(QStringLiteral("retouchMode"), QStringLiteral("dust"));
  mask_def.insert(QStringLiteral("committed"), false);
  mask_def.insert(QStringLiteral("removeStrength"), qBound(0.0, payload.remove_strength, 100.0));
  mask_def.insert(QStringLiteral("feather"), qBound(0.05, payload.feather, 1.0));
  mask_def.insert(QStringLiteral("segmentationData"), mask_data);
  mask_def.insert(QStringLiteral("segmentationWidth"), source_image.width());
  mask_def.insert(QStringLiteral("segmentationHeight"), source_image.height());
  next_stack.insert(insert_index, mask_def);
  applyMaskStackChange(next_stack, insert_index, true);
  setStatus(QStringLiteral("Sensor dust map staged. Review, then apply or discard."));
}

void EditorPreviewController::removeRedEye() {
  QImage source_image;
  if (!decodeSourceImage(&source_image, 1024)) {
    setStatus(QStringLiteral("Red-eye removal needs a decoded source image."));
    return;
  }

  const auto result = detect_red_eye_mask(source_image);
  if (!valid_segmentation_payload(result.mask_data, result.width, result.height) ||
      result.component_count <= 0) {
    setStatus(QStringLiteral("Red-eye removal found no red pupil highlights."));
    return;
  }

  auto next_stack = mask_stack_;
  const auto insert_index =
      hasActiveMask() ? qBound(0, active_mask_index_ + 1, static_cast<int>(next_stack.size()))
                      : static_cast<int>(next_stack.size());
  auto mask_def = default_mask_definition(QStringLiteral("object"), insert_index + 1);
  mask_def.insert(QStringLiteral("name"), QStringLiteral("Red-Eye Removal %1").arg(insert_index + 1));
  mask_def.insert(QStringLiteral("operation"), QStringLiteral("add"));
  mask_def.insert(QStringLiteral("toolRole"), QStringLiteral("retouch"));
  mask_def.insert(QStringLiteral("retouchMode"), QStringLiteral("red-eye"));
  mask_def.insert(QStringLiteral("committed"), false);
  mask_def.insert(QStringLiteral("removeStrength"), 100.0);
  mask_def.insert(QStringLiteral("feather"), 0.50);
  mask_def.insert(QStringLiteral("segmentationData"), result.mask_data);
  mask_def.insert(QStringLiteral("segmentationWidth"), result.width);
  mask_def.insert(QStringLiteral("segmentationHeight"), result.height);
  next_stack.insert(insert_index, mask_def);
  applyMaskStackChange(next_stack, insert_index, true);
  setStatus(QStringLiteral("Red-eye removal staged for %1 pupil%2. Review, then apply or discard.")
                .arg(result.component_count)
                .arg(result.component_count == 1 ? QString() : QStringLiteral("s")));
}

void EditorPreviewController::cleanupSkin() {
  QImage source_image;
  if (!decodeSourceImage(&source_image, 1024)) {
    setStatus(QStringLiteral("Skin cleanup needs a decoded source image."));
    return;
  }

  const auto result = detect_skin_cleanup_mask(source_image);
  if (!valid_segmentation_payload(result.mask_data, result.width, result.height) ||
      result.pixel_count <= 0) {
    setStatus(QStringLiteral("Skin cleanup found no skin-toned regions."));
    return;
  }

  auto next_stack = mask_stack_;
  const auto insert_index =
      hasActiveMask() ? qBound(0, active_mask_index_ + 1, static_cast<int>(next_stack.size()))
                      : static_cast<int>(next_stack.size());
  auto mask_def = default_mask_definition(QStringLiteral("object"), insert_index + 1);
  mask_def.insert(QStringLiteral("name"), QStringLiteral("Skin Cleanup %1").arg(insert_index + 1));
  mask_def.insert(QStringLiteral("operation"), QStringLiteral("add"));
  mask_def.insert(QStringLiteral("toolRole"), QStringLiteral("retouch"));
  mask_def.insert(QStringLiteral("retouchMode"), QStringLiteral("skin-cleanup"));
  mask_def.insert(QStringLiteral("committed"), true);
  mask_def.insert(QStringLiteral("removeStrength"), 46.0);
  mask_def.insert(QStringLiteral("feather"), 0.62);
  mask_def.insert(QStringLiteral("segmentationData"), result.mask_data);
  mask_def.insert(QStringLiteral("segmentationWidth"), result.width);
  mask_def.insert(QStringLiteral("segmentationHeight"), result.height);
  next_stack.insert(insert_index, mask_def);
  applyMaskStackChange(next_stack, insert_index, true);
  setStatus(QStringLiteral("Skin cleanup applied."));
}

void EditorPreviewController::smoothSkinFrequency() {
  QImage source_image;
  if (!decodeSourceImage(&source_image, 1024)) {
    setStatus(QStringLiteral("Frequency smoothing needs a decoded source image."));
    return;
  }

  const auto result = detect_skin_cleanup_mask(source_image);
  if (!valid_segmentation_payload(result.mask_data, result.width, result.height) ||
      result.pixel_count <= 0) {
    setStatus(QStringLiteral("Frequency smoothing found no skin-toned regions."));
    return;
  }

  auto next_stack = mask_stack_;
  const auto insert_index =
      hasActiveMask() ? qBound(0, active_mask_index_ + 1, static_cast<int>(next_stack.size()))
                      : static_cast<int>(next_stack.size());
  auto mask_def = default_mask_definition(QStringLiteral("object"), insert_index + 1);
  mask_def.insert(QStringLiteral("name"), QStringLiteral("Frequency Smoothing %1").arg(insert_index + 1));
  mask_def.insert(QStringLiteral("operation"), QStringLiteral("add"));
  mask_def.insert(QStringLiteral("toolRole"), QStringLiteral("retouch"));
  mask_def.insert(QStringLiteral("retouchMode"), QStringLiteral("frequency-smoothing"));
  mask_def.insert(QStringLiteral("committed"), true);
  mask_def.insert(QStringLiteral("removeStrength"), 58.0);
  mask_def.insert(QStringLiteral("feather"), 0.70);
  mask_def.insert(QStringLiteral("segmentationData"), result.mask_data);
  mask_def.insert(QStringLiteral("segmentationWidth"), result.width);
  mask_def.insert(QStringLiteral("segmentationHeight"), result.height);
  next_stack.insert(insert_index, mask_def);
  applyMaskStackChange(next_stack, insert_index, true);
  setStatus(QStringLiteral("Frequency-style skin smoothing applied."));
}

void EditorPreviewController::startCloneBrush() {
  const auto reusable_index = findReusableRetouchBrushIndex(QStringLiteral("clone"));
  if (reusable_index >= 0) {
    setActiveMaskIndex(reusable_index);
    setStatus(QStringLiteral("Clone Stamp ready. Pick a source if needed, then paint and apply."));
    return;
  }

  auto next_stack = mask_stack_;
  const auto insert_index =
      hasActiveMask() ? qBound(0, active_mask_index_ + 1, static_cast<int>(next_stack.size()))
                      : static_cast<int>(next_stack.size());

  auto mask_def = buildBrushMask(
      QStringLiteral("Clone Stamp %1").arg(insert_index + 1),
      QStringLiteral("add"),
      QStringLiteral("retouch"),
      QStringLiteral("clone"),
      false,
      insert_index + 1);
  mask_def.insert(QStringLiteral("brushPreset"), QStringLiteral("Clone Stamp"));
  mask_def.insert(QStringLiteral("brushSize"), 0.07);
  mask_def.insert(QStringLiteral("brushFlow"), 0.90);
  mask_def.insert(QStringLiteral("brushHardness"), 0.26);
  mask_def.insert(QStringLiteral("brushSpacing"), 0.75);
  mask_def.insert(QStringLiteral("brushSmoothing"), 0.08);
  mask_def.insert(QStringLiteral("brushBuildup"), true);
  mask_def.insert(QStringLiteral("brushEraseMode"), false);
  mask_def.insert(QStringLiteral("brushAutoMask"), false);
  mask_def.insert(QStringLiteral("brushAutoMaskTolerance"), 0.18);
  mask_def.insert(QStringLiteral("feather"), 0.74);
  mask_def.insert(QStringLiteral("cloneSourceSet"), false);
  mask_def.insert(QStringLiteral("cloneOffsetX"), 0.0);
  mask_def.insert(QStringLiteral("cloneOffsetY"), 0.0);
  mask_def.insert(QStringLiteral("cloneScale"), 1.0);
  mask_def.insert(QStringLiteral("cloneRotation"), 0.0);
  next_stack.insert(insert_index, mask_def);
  applyMaskStackChange(next_stack, insert_index, false);
  setStatus(QStringLiteral("Clone Stamp ready. Pick a source point, then paint and apply."));
}

void EditorPreviewController::applyActiveRetouch() {
  if (!hasActiveMask()) {
    return;
  }

  const auto active_mask = mask_stack_.at(active_mask_index_).toMap();
  const auto active_retouch_mode =
      active_mask.value(QStringLiteral("retouchMode")).toString().trimmed().toLower();
  if (active_mask.value(QStringLiteral("toolRole")).toString() != QStringLiteral("retouch") ||
      active_mask.value(QStringLiteral("committed")).toBool() ||
      active_retouch_mode.isEmpty()) {
    return;
  }

  auto next_stack = mask_stack_;
  auto changed = false;
  auto inpaint_result = AiInpaintResult{};
  QByteArray encoded_inpaint_patch;
  auto has_inpaint_patch = false;
  auto inpaint_error = QString{};
  QByteArray inpaint_source_mask_data;
  auto inpaint_source_mask_width = 0;
  auto inpaint_source_mask_height = 0;
  if (active_retouch_mode == QStringLiteral("remove") ||
      active_retouch_mode == QStringLiteral("heal") ||
      active_retouch_mode == QStringLiteral("patch")) {
    QImage source_image;
    const auto inpaint_decode_edge =
        inpaint_quality_mode_ == QStringLiteral("final") ? 0 : 1024;
    if (decodeSourceImage(&source_image, inpaint_decode_edge)) {
      QByteArray merged_mask_data;
      auto merged_width = 0;
      auto merged_height = 0;
      auto has_merged_mask = false;
      for (int index = 0; index < next_stack.size(); ++index) {
        const auto mask = next_stack.at(index).toMap();
        if (mask.value(QStringLiteral("toolRole")).toString() != QStringLiteral("retouch") ||
            mask.value(QStringLiteral("committed")).toBool() ||
            mask.value(QStringLiteral("retouchMode")).toString().trimmed().toLower() !=
                active_retouch_mode) {
          continue;
        }

        auto mask_data = mask.value(QStringLiteral("segmentationData")).toByteArray();
        const auto mask_width = mask.value(QStringLiteral("segmentationWidth")).toInt();
        const auto mask_height = mask.value(QStringLiteral("segmentationHeight")).toInt();
        if (!valid_segmentation_payload(mask_data, mask_width, mask_height)) {
          continue;
        }
        auto inpaint_mask_width = mask_width;
        auto inpaint_mask_height = mask_height;
        if (mask_width != source_image.width() || mask_height != source_image.height()) {
          mask_data = resample_segmentation_mask(
              mask_data,
              mask_width,
              mask_height,
              source_image.width(),
              source_image.height());
          inpaint_mask_width = source_image.width();
          inpaint_mask_height = source_image.height();
        }
        if (!valid_segmentation_payload(mask_data, inpaint_mask_width, inpaint_mask_height)) {
          continue;
        }

        merged_mask_data = union_segmentation_masks(
            merged_mask_data,
            merged_width,
            merged_height,
            mask_data,
            inpaint_mask_width,
            inpaint_mask_height);
        merged_width = inpaint_mask_width;
        merged_height = inpaint_mask_height;
        has_merged_mask = true;
      }

      if (has_merged_mask &&
          valid_segmentation_payload(merged_mask_data, merged_width, merged_height)) {
        inpaint_result = run_ai_inpaint(AiInpaintRequest{
            .source_image = source_image,
            .mask_data = merged_mask_data,
            .mask_width = merged_width,
            .mask_height = merged_height,
            .retouch_mode = active_retouch_mode,
            .prompt = inpaint_prompt_,
            .quality_mode = inpaint_quality_mode_,
            .seed = inpaint_seed_,
        });
        inpaint_source_mask_data = merged_mask_data;
        inpaint_source_mask_width = merged_width;
        inpaint_source_mask_height = merged_height;
        if (inpaint_result.success && !inpaint_result.patch_image.isNull() &&
            inpaint_result.patch_width > 0 && inpaint_result.patch_height > 0) {
          encoded_inpaint_patch = encode_png_bytes(inpaint_result.patch_image);
          has_inpaint_patch = !encoded_inpaint_patch.isEmpty();
          if (!has_inpaint_patch) {
            inpaint_error = QStringLiteral("The inpaint patch could not be encoded.");
          }
        } else {
          inpaint_error = inpaint_result.error_message;
        }
      }
    } else {
      inpaint_error = QStringLiteral("Unable to decode the source image for inpainting.");
    }
  }
  for (int index = 0; index < next_stack.size(); ++index) {
    auto mask = next_stack.at(index).toMap();
    if (mask.value(QStringLiteral("toolRole")).toString() != QStringLiteral("retouch") ||
        mask.value(QStringLiteral("committed")).toBool() ||
        mask.value(QStringLiteral("retouchMode")).toString().trimmed().toLower() != active_retouch_mode) {
      continue;
    }
    mask.insert(QStringLiteral("committed"), true);
    if (has_inpaint_patch) {
      mask.insert(QStringLiteral("inpaintPatchData"), encoded_inpaint_patch);
      mask.insert(QStringLiteral("inpaintPatchX"), inpaint_result.patch_x);
      mask.insert(QStringLiteral("inpaintPatchY"), inpaint_result.patch_y);
      mask.insert(QStringLiteral("inpaintPatchWidth"), inpaint_result.patch_width);
      mask.insert(QStringLiteral("inpaintPatchHeight"), inpaint_result.patch_height);
      mask.insert(QStringLiteral("inpaintModelName"), inpaint_result.model_name);
      if (valid_segmentation_payload(
              inpaint_source_mask_data,
              inpaint_source_mask_width,
              inpaint_source_mask_height)) {
        mask.insert(QStringLiteral("inpaintSourceMaskData"), inpaint_source_mask_data);
        mask.insert(QStringLiteral("inpaintSourceMaskWidth"), inpaint_source_mask_width);
        mask.insert(QStringLiteral("inpaintSourceMaskHeight"), inpaint_source_mask_height);
      }
      mask.insert(QStringLiteral("inpaintCandidateIndex"), inpaint_result.candidate_index);
      if (!inpaint_result.candidate_images.isEmpty()) {
        QVariantList candidates;
        for (int candidate_index = 0; candidate_index < inpaint_result.candidate_images.size();
             ++candidate_index) {
          const auto encoded_candidate = encode_png_bytes(inpaint_result.candidate_images.at(candidate_index));
          if (encoded_candidate.isEmpty()) {
            continue;
          }
          QVariantMap candidate;
          candidate.insert(QStringLiteral("patchData"), encoded_candidate);
          candidate.insert(QStringLiteral("patchX"), inpaint_result.patch_x);
          candidate.insert(QStringLiteral("patchY"), inpaint_result.patch_y);
          candidate.insert(QStringLiteral("patchWidth"), inpaint_result.patch_width);
          candidate.insert(QStringLiteral("patchHeight"), inpaint_result.patch_height);
          candidate.insert(QStringLiteral("modelName"), inpaint_result.model_name);
          candidate.insert(QStringLiteral("index"), candidate_index);
          candidate.insert(QStringLiteral("seed"), inpaint_result.seed + candidate_index);
          candidates.append(candidate);
        }
        if (!candidates.isEmpty()) {
          mask.insert(QStringLiteral("inpaintCandidates"), candidates);
          mask.insert(QStringLiteral("inpaintCandidateCount"), candidates.size());
        }
      }
      if (!inpaint_result.prompt.isEmpty()) {
        mask.insert(QStringLiteral("inpaintPrompt"), inpaint_result.prompt);
      }
      mask.insert(QStringLiteral("inpaintQualityMode"), inpaint_result.quality_mode);
      mask.insert(QStringLiteral("inpaintSeed"), inpaint_result.seed);
      mask.insert(QStringLiteral("inpaintBlendAmount"), inpaint_blend_amount_);
      mask.insert(QStringLiteral("inpaintFallback"), inpaint_result.fallback);
      mask.insert(QStringLiteral("inpaintRegionKind"), inpaint_result.region_kind);
      mask.insert(QStringLiteral("inpaintEdgeContinuation"), inpaint_result.edge_continuation);
      mask.insert(QStringLiteral("inpaintTextureSynthesis"), inpaint_result.texture_synthesis);
      mask.insert(QStringLiteral("inpaintTextureStrength"), inpaint_result.texture_strength);
      mask.insert(QStringLiteral("inpaintRegionCoverage"), inpaint_result.region_coverage);
      mask.insert(QStringLiteral("inpaintRegionX"), inpaint_result.region_x);
      mask.insert(QStringLiteral("inpaintRegionY"), inpaint_result.region_y);
      mask.insert(QStringLiteral("inpaintRegionWidth"), inpaint_result.region_width);
      mask.insert(QStringLiteral("inpaintRegionHeight"), inpaint_result.region_height);
      if (inpaint_result.fallback && !inpaint_result.error_message.isEmpty()) {
        mask.insert(QStringLiteral("inpaintFallbackReason"), inpaint_result.error_message);
      }
    }
    next_stack[index] = mask;
    changed = true;
  }
  if (!changed) {
    return;
  }

  applyMaskStackChange(next_stack, active_mask_index_, true);
  auto status =
      QStringLiteral("%1 retouch applied.")
          .arg(active_retouch_mode.left(1).toUpper() + active_retouch_mode.mid(1));
  if (active_retouch_mode == QStringLiteral("remove") ||
      active_retouch_mode == QStringLiteral("heal") ||
      active_retouch_mode == QStringLiteral("patch")) {
    if (has_inpaint_patch) {
      status += inpaint_result.fallback ? QStringLiteral(" CPU inpaint patch baked.")
                                        : QStringLiteral(" AI inpaint patch baked.");
    } else if (!inpaint_error.isEmpty()) {
      status += QStringLiteral(" CPU fill fallback rendered this edit: %1").arg(inpaint_error);
    } else {
      const auto ai_status = ai_model_runtime_status();
      status += ai_status.inpaint_available
                    ? QStringLiteral(" AI inpaint model did not return a patch.")
                    : QStringLiteral(" AI inpaint model is missing, so the CPU fill fallback rendered this edit.");
    }
  }
  setStatus(status);
}

void EditorPreviewController::regenerateInpaintSeed() {
  setInpaintSeed(inpaint_seed_ + 1);
  setStatus(QStringLiteral("Inpaint seed advanced to %1.").arg(inpaint_seed_));
}

void EditorPreviewController::cancelActiveRetouch() {
  if (!hasActiveMask()) {
    return;
  }

  const auto active_mask = mask_stack_.at(active_mask_index_).toMap();
  const auto active_retouch_mode =
      active_mask.value(QStringLiteral("retouchMode")).toString().trimmed().toLower();
  if (active_mask.value(QStringLiteral("toolRole")).toString() != QStringLiteral("retouch") ||
      active_mask.value(QStringLiteral("committed")).toBool() ||
      active_retouch_mode.isEmpty()) {
    return;
  }

  auto next_stack = mask_stack_;
  auto changed = false;
  for (qsizetype index = next_stack.size(); index > 0; --index) {
    const auto mask_index = index - 1;
    const auto mask = next_stack.at(mask_index).toMap();
    if (mask.value(QStringLiteral("toolRole")).toString() != QStringLiteral("retouch") ||
        mask.value(QStringLiteral("committed")).toBool() ||
        mask.value(QStringLiteral("retouchMode")).toString().trimmed().toLower() != active_retouch_mode) {
      continue;
    }
    next_stack.removeAt(mask_index);
    changed = true;
  }
  if (!changed) {
    return;
  }

  const auto next_index =
      next_stack.isEmpty()
          ? -1
          : qBound(0, active_mask_index_, static_cast<int>(next_stack.size()) - 1);
  applyMaskStackChange(next_stack, next_index, false);
  setStatus(
      QStringLiteral("%1 retouch cleared.")
          .arg(active_retouch_mode.left(1).toUpper() + active_retouch_mode.mid(1)));
}

void EditorPreviewController::startPolygonSelection(bool subtract) {
  auto next_stack = mask_stack_;
  const auto insert_index =
      hasActiveMask() ? qBound(0, active_mask_index_ + 1, static_cast<int>(next_stack.size()))
                      : static_cast<int>(next_stack.size());

  auto mask_def = default_mask_definition(QStringLiteral("polygon"), insert_index + 1);
  mask_def.insert(
      QStringLiteral("name"),
      subtract ? QStringLiteral("Polygon Subtract %1").arg(insert_index + 1)
               : QStringLiteral("Polygon Select %1").arg(insert_index + 1));
  mask_def.insert(QStringLiteral("operation"), subtract ? QStringLiteral("subtract")
                                                        : QStringLiteral("add"));
  if (hasActiveMask()) {
    const auto active_mask = activeMaskDefinition();
    if (is_selection_mask(active_mask)) {
      inherit_selection_mask_set(&mask_def, active_mask);
    }
  }
  mask_def.insert(QStringLiteral("feather"), 0.04);
  next_stack.insert(insert_index, mask_def);
  applyMaskStackChange(next_stack, insert_index);
  setStatus(
      subtract ? QStringLiteral("Polygon subtract started. Click to remove areas from the selection.")
                : QStringLiteral("Polygon selection started. Click to place points."));
}

void EditorPreviewController::startLassoSelection(bool subtract) {
  auto next_stack = mask_stack_;
  const auto insert_index =
      hasActiveMask() ? qBound(0, active_mask_index_ + 1, static_cast<int>(next_stack.size()))
                      : static_cast<int>(next_stack.size());

  auto mask_def = default_mask_definition(QStringLiteral("lasso"), insert_index + 1);
  mask_def.insert(
      QStringLiteral("name"),
      subtract ? QStringLiteral("Lasso Subtract %1").arg(insert_index + 1)
               : QStringLiteral("Lasso Select %1").arg(insert_index + 1));
  mask_def.insert(QStringLiteral("operation"), subtract ? QStringLiteral("subtract")
                                                        : QStringLiteral("add"));
  if (hasActiveMask()) {
    const auto active_mask = activeMaskDefinition();
    if (is_selection_mask(active_mask)) {
      inherit_selection_mask_set(&mask_def, active_mask);
    }
  }
  mask_def.insert(QStringLiteral("feather"), 0.04);
  next_stack.insert(insert_index, mask_def);
  applyMaskStackChange(next_stack, insert_index);
  setStatus(
      subtract ? QStringLiteral("Lasso subtract started. Drag to remove areas from the selection.")
               : QStringLiteral("Lasso selection started. Drag to define the selection."));
}

void EditorPreviewController::appendActivePolygonPoint(double normalized_x, double normalized_y) {
  auto mask = activeMaskDefinition();
  const auto kind = activeMaskKind();
  if (mask.isEmpty() || (kind != QStringLiteral("polygon") && kind != QStringLiteral("lasso"))) {
    return;
  }

  QVariantMap point;
  point.insert(QStringLiteral("x"), qBound(0.0, normalized_x, 1.0));
  point.insert(QStringLiteral("y"), qBound(0.0, normalized_y, 1.0));

  auto points = mask.value(QStringLiteral("polygonPoints")).toList();
  if (!points.isEmpty()) {
    const auto last = points.constLast().toMap();
    const auto dx = point.value(QStringLiteral("x")).toDouble() - last.value(QStringLiteral("x")).toDouble();
    const auto dy = point.value(QStringLiteral("y")).toDouble() - last.value(QStringLiteral("y")).toDouble();
    if (std::hypot(dx, dy) < 0.004) {
      return;
    }
  }
  points.append(point);
  mask.insert(QStringLiteral("polygonPoints"), points);
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::removeLastActivePolygonPoint() {
  auto mask = activeMaskDefinition();
  const auto kind = activeMaskKind();
  if (mask.isEmpty() || (kind != QStringLiteral("polygon") && kind != QStringLiteral("lasso"))) {
    return;
  }
  auto points = mask.value(QStringLiteral("polygonPoints")).toList();
  if (points.isEmpty()) {
    return;
  }
  points.removeLast();
  mask.insert(QStringLiteral("polygonPoints"), points);
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::clearActivePolygonPoints() {
  auto mask = activeMaskDefinition();
  const auto kind = activeMaskKind();
  if (mask.isEmpty() || (kind != QStringLiteral("polygon") && kind != QStringLiteral("lasso"))) {
    return;
  }
  mask.insert(QStringLiteral("polygonPoints"), QVariantList{});
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::duplicateActiveMask() {
  const auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }

  auto next_stack = mask_stack_;
  next_stack.insert(active_mask_index_ + 1, mask);
  applyMaskStackChange(next_stack, active_mask_index_ + 1);
}

void EditorPreviewController::removeActiveMask() {
  if (!hasActiveMask() || activeMaskLocked()) {
    return;
  }

  auto next_stack = mask_stack_;
  next_stack.removeAt(active_mask_index_);
  const auto next_index =
      next_stack.isEmpty()
          ? -1
          : qBound(0, active_mask_index_, static_cast<int>(next_stack.size()) - 1);
  applyMaskStackChange(next_stack, next_index);
}

void EditorPreviewController::moveActiveMask(int offset) {
  if (!hasActiveMask() || activeMaskLocked() || offset == 0) {
    return;
  }

  const auto target_index = qBound(
      0, active_mask_index_ + offset, static_cast<int>(mask_stack_.size()) - 1);
  if (target_index == active_mask_index_) {
    return;
  }

  auto next_stack = mask_stack_;
  next_stack.move(active_mask_index_, target_index);
  applyMaskStackChange(next_stack, target_index);
}

void EditorPreviewController::beginActiveBrushStroke() {
  if (brush_stroke_active_ || !hasActiveMask() || activeMaskKind() != QStringLiteral("brush")) {
    return;
  }

  brush_stroke_undo_state_valid_ = !asset_id_.isEmpty() && !applying_history_state_;
  if (brush_stroke_undo_state_valid_) {
    brush_stroke_undo_state_ = currentState();
  }
  brush_stroke_active_ = true;
  brush_stroke_suspends_side_effects_ = true;
  brush_stroke_needs_final_render_ = false;
  active_brush_live_points_.clear();
  clearActiveBrushLivePreview();
  emit activeBrushStrokeChanged();
}

void EditorPreviewController::endActiveBrushStroke() {
  if (!brush_stroke_active_) {
    return;
  }

  if (hasActiveMask() && activeMaskKind() == QStringLiteral("brush") &&
      !active_brush_live_points_.isEmpty()) {
    auto mask = activeMaskDefinition();
    auto points = mask.value(QStringLiteral("brushPoints")).toList();
    const auto existing_point_count = points.size();
    for (const auto& point : active_brush_live_points_) {
      points.append(brush_point_variant(point));
    }
    mask.insert(QStringLiteral("brushPoints"), points);

    if (activeMaskRetouchMode() == QStringLiteral("clone") &&
        map_bool_value(mask, QStringLiteral("cloneSourceSet"), false) &&
        existing_point_count == 0) {
      const auto& first_point = active_brush_live_points_.constFirst();
      mask.insert(
          QStringLiteral("cloneOffsetX"),
          map_double_value(mask, QStringLiteral("cloneSourceX"), 0.5) -
              first_point.x);
      mask.insert(
          QStringLiteral("cloneOffsetY"),
          map_double_value(mask, QStringLiteral("cloneSourceY"), 0.5) -
              first_point.y);
    }

    const auto rerender =
        mask.value(QStringLiteral("toolRole")).toString() != QStringLiteral("retouch") ||
        mask.value(QStringLiteral("committed")).toBool();
    QImage source_image;
    const auto existing_segmentation =
        mask.value(QStringLiteral("segmentationData")).toByteArray();
    const auto existing_width = mask.value(QStringLiteral("segmentationWidth")).toInt();
    const auto existing_height = mask.value(QStringLiteral("segmentationHeight")).toInt();
    const auto can_incrementally_update =
        decodeSourceImage(&source_image, 1024) &&
        valid_segmentation_payload(existing_segmentation, existing_width, existing_height) &&
        existing_width == source_image.width() &&
        existing_height == source_image.height();
    if (can_incrementally_update) {
      for (const auto& point : active_brush_live_points_) {
        rasterize_brush_point_into_mask(
            &mask,
            &source_image,
            point.x,
            point.y,
            point.size,
            point.flow,
            activeMaskFeather(),
            point.erase,
            activeMaskBrushAutoMask(),
            activeMaskBrushAutoMaskTolerance());
      }
      updateActiveMaskDefinition(mask, false);
    } else {
      updateActiveMaskDefinition(mask, false);
      (void)rebuildActiveBrushSegmentation(1024);
    }
    if (rerender) {
      brush_stroke_needs_final_render_ = true;
    }
  }

  brush_stroke_active_ = false;
  brush_stroke_suspends_side_effects_ = false;
  active_brush_live_points_.clear();
  clearActiveBrushLivePreview();
  if (brush_stroke_undo_state_valid_ &&
      !edit_states_equal(brush_stroke_undo_state_, currentState())) {
    pushUndoState(brush_stroke_undo_state_);
    redoStackForAsset(asset_id_).clear();
    emit historyChanged();
  }
  brush_stroke_undo_state_valid_ = false;
  persistCurrentState();
  if (brush_stroke_needs_final_render_) {
    renderPreview(true, QStringLiteral("mask_preview_change"));
  }
  brush_stroke_needs_final_render_ = false;
  emit activeBrushStrokeChanged();
}

void EditorPreviewController::addBrushPoint(double normalized_x, double normalized_y) {
  addBrushPointWithPressure(normalized_x, normalized_y, 1.0);
}

void EditorPreviewController::addBrushPointWithPressure(
    double normalized_x,
    double normalized_y,
    double pressure) {
  addBrushPointWithTablet(normalized_x, normalized_y, pressure, 0.0, 0.0);
}

void EditorPreviewController::addBrushPointWithTablet(
    double normalized_x,
    double normalized_y,
    double pressure,
    double tilt_x,
    double tilt_y) {
  addBrushPointWithTabletEraser(normalized_x, normalized_y, pressure, tilt_x, tilt_y, false);
}

void EditorPreviewController::addBrushPointWithTabletEraser(
    double normalized_x,
    double normalized_y,
    double pressure,
    double tilt_x,
    double tilt_y,
    bool eraser) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("brush")) {
    return;
  }

  const auto brush_size = activeMaskBrushSize();
  const auto brush_flow = activeMaskBrushFlow();
  const auto brush_spacing = activeMaskBrushSpacing();
  const auto brush_smoothing = activeMaskBrushSmoothing();
  const auto brush_erase = eraser || activeMaskBrushEraseMode();
  const auto brush_pressure = clamp_unit(pressure);
  const auto brush_tilt_x = std::clamp(tilt_x, -1.0, 1.0);
  const auto brush_tilt_y = std::clamp(tilt_y, -1.0, 1.0);
  const auto retouch_mode = activeMaskRetouchMode();
  auto points = mask.value(QStringLiteral("brushPoints")).toList();
  BrushPreviewPoint last_point;
  const BrushPreviewPoint* last_point_ptr = nullptr;
  if (brush_stroke_active_) {
    if (!active_brush_live_points_.isEmpty()) {
      last_point = active_brush_live_points_.constLast();
      last_point_ptr = &last_point;
    }
  } else if (!points.isEmpty()) {
    last_point = brush_preview_point_from_variant(
        points.constLast(),
        normalized_x,
        normalized_y,
        brush_size,
        brush_flow);
    last_point_ptr = &last_point;
  }
  const auto sampled_points =
      sampled_brush_points_for_segment(
          last_point_ptr,
          normalized_x,
          normalized_y,
          brush_size,
          brush_flow,
          brush_spacing,
          brush_smoothing,
          brush_erase,
          brush_pressure,
          brush_tilt_x,
          brush_tilt_y);
  if (sampled_points.isEmpty()) {
    return;
  }

  if (brush_stroke_active_) {
    for (const auto& point : sampled_points) {
      active_brush_live_points_.append(point);
    }
    updateActiveBrushLivePreview(sampled_points);
    emit activeBrushStrokeChanged();
    return;
  }

  if (retouch_mode == QStringLiteral("clone") &&
      map_bool_value(mask, QStringLiteral("cloneSourceSet"), false) &&
      points.isEmpty()) {
    const auto& first_point = sampled_points.constFirst();
    mask.insert(
        QStringLiteral("cloneOffsetX"),
        map_double_value(mask, QStringLiteral("cloneSourceX"), 0.5) -
            first_point.x);
    mask.insert(
        QStringLiteral("cloneOffsetY"),
        map_double_value(mask, QStringLiteral("cloneSourceY"), 0.5) -
            first_point.y);
  }

  for (const auto& point : sampled_points) {
    points.append(brush_point_variant(point));
  }
  mask.insert(QStringLiteral("brushPoints"), points);
  QImage source_image;
  const auto preview_edge = brush_stroke_active_ ? 640 : 1024;
  const auto has_preview_source = decodeSourceImage(&source_image, preview_edge);
  if (has_preview_source &&
      ensure_brush_raster_mask(&mask, source_image.size())) {
    if (mask.value(QStringLiteral("segmentationWidth")).toInt() != source_image.width() ||
        mask.value(QStringLiteral("segmentationHeight")).toInt() != source_image.height()) {
      mask.insert(
          QStringLiteral("segmentationData"),
          QByteArray(source_image.width() * source_image.height(), '\0'));
      mask.insert(QStringLiteral("segmentationWidth"), source_image.width());
      mask.insert(QStringLiteral("segmentationHeight"), source_image.height());
    }
    for (const auto& point : sampled_points) {
      rasterize_brush_point_into_mask(
          &mask,
          &source_image,
          point.x,
          point.y,
          point.size,
          point.flow,
          activeMaskFeather(),
          point.erase,
          activeMaskBrushAutoMask(),
          activeMaskBrushAutoMaskTolerance());
    }
  }
  const auto rerender =
      mask.value(QStringLiteral("toolRole")).toString() != QStringLiteral("retouch") ||
      mask.value(QStringLiteral("committed")).toBool();
  if (brush_stroke_active_ && rerender) {
    brush_stroke_needs_final_render_ = true;
  }
  updateActiveMaskDefinition(mask, brush_stroke_active_ ? false : rerender);
}

void EditorPreviewController::clearActiveBrushPoints() {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("brush")) {
    return;
  }
  active_brush_live_points_.clear();
  clearActiveBrushLivePreview();
  mask.insert(QStringLiteral("brushPoints"), QVariantList{});
  if (activeMaskKind() == QStringLiteral("brush")) {
    mask.insert(QStringLiteral("segmentationData"), QByteArray{});
    mask.insert(QStringLiteral("segmentationWidth"), 0);
    mask.insert(QStringLiteral("segmentationHeight"), 0);
  }
  const auto rerender =
      mask.value(QStringLiteral("toolRole")).toString() != QStringLiteral("retouch") ||
      mask.value(QStringLiteral("committed")).toBool();
  updateActiveMaskDefinition(mask, rerender);
  emit activeBrushStrokeChanged();
}

void EditorPreviewController::setActiveMaskCloneSource(double normalized_x, double normalized_y) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("brush") ||
      activeMaskRetouchMode() != QStringLiteral("clone")) {
    return;
  }
  mask.insert(QStringLiteral("cloneSourceSet"), true);
  mask.insert(QStringLiteral("cloneSourceX"), qBound(0.0, normalized_x, 1.0));
  mask.insert(QStringLiteral("cloneSourceY"), qBound(0.0, normalized_y, 1.0));
  mask.insert(QStringLiteral("cloneOffsetX"), 0.0);
  mask.insert(QStringLiteral("cloneOffsetY"), 0.0);
  active_brush_live_points_.clear();
  clearActiveBrushLivePreview();
  mask.insert(QStringLiteral("brushPoints"), QVariantList{});
  updateActiveMaskDefinition(mask, false);
  emit activeBrushStrokeChanged();
  setStatus(QStringLiteral("Clone source picked. Paint to stamp from that area."));
}

bool EditorPreviewController::rebuildActiveBrushSegmentation(int max_edge) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("brush")) {
    return false;
  }

  const auto points = mask.value(QStringLiteral("brushPoints")).toList();
  if (points.isEmpty()) {
    mask.insert(QStringLiteral("segmentationData"), QByteArray{});
    mask.insert(QStringLiteral("segmentationWidth"), 0);
    mask.insert(QStringLiteral("segmentationHeight"), 0);
    updateActiveMaskDefinition(mask, false);
    return false;
  }

  QImage source_image;
  if (!decodeSourceImage(&source_image, max_edge)) {
    return false;
  }

  if (!ensure_brush_raster_mask(&mask, source_image.size())) {
    return false;
  }
  mask.insert(
      QStringLiteral("segmentationData"),
      QByteArray(source_image.width() * source_image.height(), '\0'));
  mask.insert(QStringLiteral("segmentationWidth"), source_image.width());
  mask.insert(QStringLiteral("segmentationHeight"), source_image.height());
  for (const auto& point_value : points) {
    const auto point = point_value.toMap();
    rasterize_brush_point_into_mask(
        &mask,
        &source_image,
        map_double_value(point, QStringLiteral("x"), 0.5),
        map_double_value(point, QStringLiteral("y"), 0.5),
        map_double_value(point, QStringLiteral("size"), activeMaskBrushSize()),
        map_double_value(point, QStringLiteral("flow"), activeMaskBrushFlow()),
        activeMaskFeather(),
        map_bool_value(point, QStringLiteral("erase"), false),
        activeMaskBrushAutoMask(),
        activeMaskBrushAutoMaskTolerance());
  }
  updateActiveMaskDefinition(mask, false);
  return true;
}

void EditorPreviewController::setWhiteBalanceSampling(bool enabled) {
  if (white_balance_sampling_ == enabled) {
    return;
  }
  if (enabled && crop_editing_) {
    crop_editing_ = false;
    emit controlsChanged();
  }
  white_balance_sampling_ = enabled;
  emit whiteBalanceSamplingChanged();
}

void EditorPreviewController::sampleWhiteBalanceAt(double normalized_x, double normalized_y) {
  if (!white_balance_sampling_) {
    return;
  }

  QImage image;
  if (!decodeSourceImage(&image, 0)) {
    setStatus(QStringLiteral("Unable to decode source for white balance sampling."));
    setWhiteBalanceSampling(false);
    return;
  }

  image = applyCrop(image);

  const auto sample_x = qBound(0, static_cast<int>(qRound(normalized_x * (image.width() - 1))), image.width() - 1);
  const auto sample_y = qBound(0, static_cast<int>(qRound(normalized_y * (image.height() - 1))), image.height() - 1);
  const auto patch_radius = qBound(
      1,
      static_cast<int>(qRound(static_cast<double>(std::min(image.width(), image.height())) * 0.015)),
      12);
  const auto patch_sample = sample_white_balance_patch(image, sample_x, sample_y, patch_radius);
  if (!patch_sample.valid) {
    setStatus(QStringLiteral("Unable to measure white balance from the selected patch."));
    setWhiteBalanceSampling(false);
    return;
  }

  const auto red = patch_sample.red;
  const auto green = patch_sample.green;
  const auto blue = patch_sample.blue;
  const auto average_rb = qMax(1e-6, (red + blue) * 0.5);
  const auto red_blue_balance = std::log2((blue + 1e-6) / (red + 1e-6));
  const auto green_balance = std::log2((green + 1e-6) / average_rb);

  beginEditTransaction();
  if (rawControlsAvailable()) {
    setRawWhiteBalanceOverride(true);
    setRawWhiteBalanceTemperature(raw_white_balance_temperature_ + red_blue_balance * 1800.0);
    setRawWhiteBalanceTint(raw_white_balance_tint_ + green_balance * 100.0);
  } else {
    setTemperature(temperature_ + red_blue_balance * 55.0);
    setTint(tint_ + green_balance * 60.0);
  }
  setWhiteBalanceSampling(false);
  endEditTransaction();
  setStatus(
      QStringLiteral("White balance sampled from a %1 px patch.")
          .arg(patch_radius * 2 + 1));
}

void EditorPreviewController::updatePixelInspector(double normalized_x, double normalized_y) {
  normalized_x = std::clamp(normalized_x, 0.0, 1.0);
  normalized_y = std::clamp(normalized_y, 0.0, 1.0);

  QVariantMap inspector;
  inspector.insert(QStringLiteral("active"), false);
  inspector.insert(QStringLiteral("normalizedX"), normalized_x);
  inspector.insert(QStringLiteral("normalizedY"), normalized_y);

  const auto after = pixel_sample_map(pixel_inspector_preview_image_, normalized_x, normalized_y);
  const auto before = pixel_sample_map(pixel_inspector_reference_image_, normalized_x, normalized_y);
  inspector.insert(QStringLiteral("after"), after);
  inspector.insert(QStringLiteral("before"), before);
  inspector.insert(QStringLiteral("hasAfter"), after.value(QStringLiteral("valid")).toBool());
  inspector.insert(QStringLiteral("hasBefore"), before.value(QStringLiteral("valid")).toBool());

  if (after.value(QStringLiteral("valid")).toBool()) {
    inspector.insert(QStringLiteral("active"), true);
    inspector.insert(QStringLiteral("pixelX"), after.value(QStringLiteral("pixelX")).toInt());
    inspector.insert(QStringLiteral("pixelY"), after.value(QStringLiteral("pixelY")).toInt());
  }

  if (after.value(QStringLiteral("valid")).toBool() && before.value(QStringLiteral("valid")).toBool()) {
    inspector.insert(
        QStringLiteral("deltaRed"),
        after.value(QStringLiteral("red")).toInt() - before.value(QStringLiteral("red")).toInt());
    inspector.insert(
        QStringLiteral("deltaGreen"),
        after.value(QStringLiteral("green")).toInt() - before.value(QStringLiteral("green")).toInt());
    inspector.insert(
        QStringLiteral("deltaBlue"),
        after.value(QStringLiteral("blue")).toInt() - before.value(QStringLiteral("blue")).toInt());
    inspector.insert(
        QStringLiteral("deltaLuminance"),
        after.value(QStringLiteral("luminance")).toDouble() -
            before.value(QStringLiteral("luminance")).toDouble());
  }

  if (pixel_inspector_ == inspector) {
    return;
  }
  pixel_inspector_ = inspector;
  emit pixelInspectorChanged();
}

void EditorPreviewController::clearPixelInspector() {
  if (pixel_inspector_.isEmpty()) {
    return;
  }
  pixel_inspector_.clear();
  emit pixelInspectorChanged();
}

void EditorPreviewController::renderPreview(bool schedule_full_resolution, const QString& reason) {
  const auto normalized_reason = normalized_render_reason(reason, schedule_full_resolution);
  if (edit_transaction_depth_ > 0) {
    edit_transaction_render_requested_ = true;
    edit_transaction_schedule_full_resolution_ =
        edit_transaction_schedule_full_resolution_ || schedule_full_resolution;
    if (edit_transaction_render_reason_.isEmpty() ||
        edit_transaction_render_reason_ == QStringLiteral("edit_change") ||
        edit_transaction_render_reason_ == QStringLiteral("interactive_change") ||
        render_reason_invalidates_preview_overlay(normalized_reason)) {
      edit_transaction_render_reason_ = normalized_reason;
    }
    return;
  }

  const auto can_coalesce_with_pending_timer =
      interactive_render_timer_.isActive() &&
      !interactive_render_watcher_.isRunning() &&
      !full_render_watcher_.isRunning();
  if (can_coalesce_with_pending_timer) {
    ++render_coalesce_request_count_;
    ++render_generation_;
    latest_render_generation_->store(render_generation_, std::memory_order_relaxed);
    ++overlay_generation_;
    latest_overlay_generation_->store(overlay_generation_, std::memory_order_relaxed);
    const auto state = currentState();
    next_interactive_render_draft_ = true;
    last_render_request_msecs_ = QDateTime::currentMSecsSinceEpoch();
    render_requested_at_msecs_ = last_render_request_msecs_;
    render_reason_ = normalized_reason;
    interactive_render_timer_.setInterval(interactive_render_debounce_ms_for_state(state));
    interactive_render_timer_.start();
    if (schedule_full_resolution) {
      full_render_timer_.stop();
      pending_full_request_ = currentRenderRequest(true);
      full_request_pending_ = true;
    } else {
      full_render_timer_.stop();
      full_request_pending_ = false;
    }
    observability::logInfo(
        photoEditorPreviewLog(),
        QStringLiteral("preview.render.coalesced"),
        {
            {QStringLiteral("file_path"), file_path_},
            {QStringLiteral("render_reason"), render_reason_},
            {QStringLiteral("interaction_mode"), QStringLiteral("draft_interactive")},
            {QStringLiteral("schedule_full_resolution"), schedule_full_resolution},
            {QStringLiteral("coalesced_request_count"), render_coalesce_request_count_},
        });
    updateProcessingState();
    return;
  }

  render_coalesce_request_count_ = 1;
  performRenderPreview(schedule_full_resolution, render_coalesce_request_count_, normalized_reason);
  render_coalesce_request_count_ = 0;
}

void EditorPreviewController::beginEditTransaction() {
  ++edit_transaction_depth_;
}

void EditorPreviewController::endEditTransaction() {
  if (edit_transaction_depth_ <= 0) {
    return;
  }

  --edit_transaction_depth_;
  if (edit_transaction_depth_ > 0 || !edit_transaction_render_requested_) {
    return;
  }

  const auto schedule_full_resolution = edit_transaction_schedule_full_resolution_;
  const auto reason = edit_transaction_render_reason_;
  edit_transaction_render_requested_ = false;
  edit_transaction_schedule_full_resolution_ = false;
  edit_transaction_render_reason_.clear();
  renderPreview(schedule_full_resolution, reason);
}

void EditorPreviewController::performRenderPreview(
    bool schedule_full_resolution,
    int coalesced_request_count,
    const QString& reason) {
  const auto now_msecs = QDateTime::currentMSecsSinceEpoch();
  const auto rapid_followup = last_render_request_msecs_ > 0 &&
                              now_msecs - last_render_request_msecs_ <=
                                  kInteractiveDraftBurstWindowMs;
  const auto force_draft_interactive = force_draft_interactive_for_reason(reason);
  next_interactive_render_draft_ =
      force_draft_interactive || rapid_followup || interactive_render_timer_.isActive() ||
      interactive_render_watcher_.isRunning() || interactive_request_pending_;
  last_render_request_msecs_ = now_msecs;
  render_requested_at_msecs_ = now_msecs;
  render_reason_ = normalized_render_reason(reason, schedule_full_resolution);
  ++render_generation_;
  latest_render_generation_->store(render_generation_, std::memory_order_relaxed);
  ++overlay_generation_;
  latest_overlay_generation_->store(overlay_generation_, std::memory_order_relaxed);
  if (interactive_request_pending_ && pending_interactive_request_.overlay_only) {
    interactive_request_pending_ = false;
  }
  if (render_reason_invalidates_preview_overlay(render_reason_)) {
    display_preview_overlay_source_.clear();
    display_reference_preview_overlay_source_.clear();
    display_preview_overlay_rect_ = QRectF();
    if (image_provider_ != nullptr) {
      image_provider_->clearImage(QString::fromLatin1(kEditedPreviewOverlayImageId));
      image_provider_->clearImage(QString::fromLatin1(kReferencePreviewOverlayImageId));
    }
  }
  const auto state = currentState();
  if (!next_interactive_render_draft_) {
    observability::logInfo(
        photoEditorPreviewLog(),
        QStringLiteral("preview.render.requested"),
        {
            {QStringLiteral("file_path"), file_path_},
            {QStringLiteral("render_reason"), render_reason_},
            {QStringLiteral("interaction_mode"), next_interactive_render_draft_ ? QStringLiteral("draft_interactive")
                                                                                : QStringLiteral("interactive")},
            {QStringLiteral("has_adjustments"), has_visible_adjustments(state)},
            {QStringLiteral("schedule_full_resolution"), schedule_full_resolution},
            {QStringLiteral("coalesced_request_count"), coalesced_request_count},
        });
  }
  if (!preview_supported(file_path_)) {
    preview_source_ = fallback_thumbnail_source_;
    display_preview_source_ = fallback_thumbnail_source_;
    display_preview_overlay_source_.clear();
    reference_preview_source_ = compare_mode_ ? fallback_thumbnail_source_ : QString();
    display_reference_preview_source_ = compare_mode_ ? fallback_thumbnail_source_ : QString();
    display_reference_preview_overlay_source_.clear();
    display_preview_overlay_rect_ = QRectF();
    if (image_provider_ != nullptr) {
      image_provider_->clearImage(QString::fromLatin1(kEditedPreviewOverlayImageId));
      image_provider_->clearImage(QString::fromLatin1(kReferencePreviewOverlayImageId));
    }
    setStatus(hasAsset() ? QStringLiteral("Preview fallback active.") : QStringLiteral("No asset selected."));
    interactive_render_timer_.stop();
    full_render_timer_.stop();
    interactive_request_pending_ = false;
    full_request_pending_ = false;
    updateProcessingState();
    refreshDebugInfo();
    emit previewChanged();
    return;
  }

  const QFileInfo source_info(file_path_);
  if (!source_info.exists() || !source_info.isReadable()) {
    preview_source_ = fallback_thumbnail_source_;
    display_preview_source_ = fallback_thumbnail_source_;
    display_preview_overlay_source_.clear();
    reference_preview_source_ = compare_mode_ ? fallback_thumbnail_source_ : QString();
    display_reference_preview_source_ = compare_mode_ ? fallback_thumbnail_source_ : QString();
    display_reference_preview_overlay_source_.clear();
    display_preview_overlay_rect_ = QRectF();
    if (image_provider_ != nullptr) {
      image_provider_->clearImage(QString::fromLatin1(kEditedPreviewOverlayImageId));
      image_provider_->clearImage(QString::fromLatin1(kReferencePreviewOverlayImageId));
    }
    setStatus(QStringLiteral("Source file unavailable."));
    interactive_render_timer_.stop();
    full_render_timer_.stop();
    interactive_request_pending_ = false;
    full_request_pending_ = false;
    updateProcessingState();
    refreshDebugInfo();
    emit previewChanged();
    return;
  }

  interactive_render_timer_.setInterval(interactive_render_debounce_ms_for_state(state));
  setStatus(has_visible_adjustments(state) ? QStringLiteral("Rendering interactive preview...") : QStringLiteral("Rendering base preview..."));
  interactive_render_timer_.start();
  if (schedule_full_resolution) {
    if (next_interactive_render_draft_) {
      full_render_timer_.stop();
      pending_full_request_ = currentRenderRequest(true);
      full_request_pending_ = true;
    } else {
      full_render_timer_.setInterval(full_render_debounce_ms_for_state(state));
      full_render_timer_.start();
    }
  } else {
    full_render_timer_.stop();
    full_request_pending_ = false;
  }
  updateProcessingState();
}

void EditorPreviewController::renderPreviewStage(bool full_resolution) {
  const auto request = currentRenderRequest(full_resolution);
  if (request.file_path.isEmpty()) {
    updateProcessingState();
    return;
  }

  observability::logInfo(
      photoEditorPreviewLog(),
      QStringLiteral("preview.render.stage_started"),
      {
          {QStringLiteral("stage"), full_resolution ? QStringLiteral("full") : QStringLiteral("interactive")},
          {QStringLiteral("file_path"), request.file_path},
          {QStringLiteral("generation"), request.generation},
          {QStringLiteral("render_reason"), request.render_reason},
          {QStringLiteral("interaction_mode"), request.interaction_mode},
      });

  auto& watcher = full_resolution ? full_render_watcher_ : interactive_render_watcher_;
  auto& pending_request = full_resolution ? pending_full_request_ : pending_interactive_request_;
  auto& pending = full_resolution ? full_request_pending_ : interactive_request_pending_;

  if (full_resolution &&
      (interactive_render_timer_.isActive() || interactive_render_watcher_.isRunning() || interactive_request_pending_)) {
    pending_request = request;
    pending = true;
    updateProcessingState();
    return;
  }

  if (watcher.isRunning()) {
    pending_request = request;
    pending = true;
    updateProcessingState();
    return;
  }

  startRenderStage(full_resolution, request);
}

void EditorPreviewController::handleRenderFinished(bool full_resolution) {
  auto& watcher = full_resolution ? full_render_watcher_ : interactive_render_watcher_;
  const auto result = watcher.result();
  if (result.cancelled) {
    auto& pending_request = full_resolution ? pending_full_request_ : pending_interactive_request_;
    auto& pending = full_resolution ? full_request_pending_ : interactive_request_pending_;
    if (pending) {
      pending = false;
      startRenderStage(full_resolution, pending_request);
      return;
    }

    if (!full_resolution && full_request_pending_ && !full_render_watcher_.isRunning()) {
      full_request_pending_ = false;
      startRenderStage(true, pending_full_request_);
      return;
    }

    updateProcessingState();
    return;
  }
  if (result.overlay_only) {
    if (result.generation == latest_overlay_generation_->load(std::memory_order_relaxed) &&
        result.generation >= last_overlay_preview_generation_) {
      last_overlay_preview_generation_ = result.generation;
      if (image_provider_ != nullptr) {
        if (!result.preview_overlay_image.isNull()) {
          image_provider_->setImage(
              QString::fromLatin1(kEditedPreviewOverlayImageId),
              result.preview_overlay_image);
        } else {
          image_provider_->clearImage(QString::fromLatin1(kEditedPreviewOverlayImageId));
        }
      }
      display_preview_overlay_source_ =
          result.display_preview_overlay_source.isEmpty()
              ? QString()
              : withDisplayRevision(result.display_preview_overlay_source);
      display_preview_overlay_rect_ = result.preview_overlay_rect;
      emit previewChanged();
    }

    auto& pending_request = full_resolution ? pending_full_request_ : pending_interactive_request_;
    auto& pending = full_resolution ? full_request_pending_ : interactive_request_pending_;
    if (pending) {
      pending = false;
      startRenderStage(full_resolution, pending_request);
      return;
    }

    if (!full_resolution && full_request_pending_ && !full_render_watcher_.isRunning()) {
      full_request_pending_ = false;
      startRenderStage(true, pending_full_request_);
      return;
    }

    updateProcessingState();
    return;
  }
  const auto can_apply_result = [&]() {
    if (full_resolution) {
      return result.generation == render_generation_ && result.generation >= last_full_preview_generation_;
    }
    return result.generation == render_generation_ &&
           result.generation >= last_interactive_preview_generation_;
  }();

  if (can_apply_result) {
    if (!result.success) {
      if (result.generation == render_generation_) {
        preview_source_ = fallback_thumbnail_source_;
        display_preview_source_ = fallback_thumbnail_source_;
        display_preview_overlay_source_.clear();
        reference_preview_source_ = compare_mode_ ? fallback_thumbnail_source_ : QString();
        display_reference_preview_source_ = compare_mode_ ? fallback_thumbnail_source_ : QString();
        display_reference_preview_overlay_source_.clear();
        display_preview_overlay_rect_ = QRectF();
        pixel_inspector_preview_image_ = QImage();
        pixel_inspector_reference_image_ = QImage();
        clearPixelInspector();
        if (display_preview_texture_ != nullptr) {
          display_preview_texture_->clear();
          ++display_preview_native_revision_;
        }
        if (image_provider_ != nullptr) {
          image_provider_->clearImage(QString::fromLatin1(kEditedPreviewOverlayImageId));
          image_provider_->clearImage(QString::fromLatin1(kReferencePreviewOverlayImageId));
        }
        setStatus(result.status);
      }
    } else {
      ++preview_revision_;
      if (!result.preview_image.isNull()) {
        pixel_inspector_preview_image_ = result.preview_image;
      }
      if (!result.reference_preview_image.isNull()) {
        pixel_inspector_reference_image_ = result.reference_preview_image;
      } else if (!compare_mode_) {
        pixel_inspector_reference_image_ = QImage();
      }
      if (pixel_inspector_.value(QStringLiteral("active")).toBool()) {
        updatePixelInspector(
            pixel_inspector_.value(QStringLiteral("normalizedX")).toDouble(),
            pixel_inspector_.value(QStringLiteral("normalizedY")).toDouble());
      }
      if (image_provider_ != nullptr && !result.preview_image.isNull()) {
        image_provider_->setImage(QString::fromLatin1(kEditedPreviewImageId), result.preview_image);
      }
      if (!result.preview_image.isNull() && display_preview_texture_ != nullptr &&
          display_preview_texture_->upload(result.preview_image)) {
        ++display_preview_native_revision_;
      }
      if (image_provider_ != nullptr) {
        if (!result.reference_preview_image.isNull()) {
          image_provider_->setImage(QString::fromLatin1(kReferencePreviewImageId), result.reference_preview_image);
        } else if (!compare_mode_) {
          image_provider_->clearImage(QString::fromLatin1(kReferencePreviewImageId));
        }
        if (!result.preview_overlay_image.isNull()) {
          image_provider_->setImage(
              QString::fromLatin1(kEditedPreviewOverlayImageId),
              result.preview_overlay_image);
        } else {
          image_provider_->clearImage(QString::fromLatin1(kEditedPreviewOverlayImageId));
        }
        if (!result.reference_overlay_image.isNull()) {
          image_provider_->setImage(
              QString::fromLatin1(kReferencePreviewOverlayImageId),
              result.reference_overlay_image);
        } else {
          image_provider_->clearImage(QString::fromLatin1(kReferencePreviewOverlayImageId));
        }
      }
      if (result.updated_persistent_sources) {
        preview_source_ = with_preview_revision(result.preview_source, preview_revision_);
        reference_preview_source_ =
            with_preview_revision(result.reference_preview_source, preview_revision_);
      }
      display_preview_source_ =
          result.display_preview_source.isEmpty()
              ? preview_source_
              : withDisplayRevision(result.display_preview_source);
      display_reference_preview_source_ =
          result.display_reference_preview_source.isEmpty()
              ? reference_preview_source_
              : withDisplayRevision(result.display_reference_preview_source);
      display_preview_overlay_source_ =
          result.display_preview_overlay_source.isEmpty()
              ? QString()
              : withDisplayRevision(result.display_preview_overlay_source);
      display_reference_preview_overlay_source_ =
          result.display_reference_preview_overlay_source.isEmpty()
              ? QString()
              : withDisplayRevision(result.display_reference_preview_overlay_source);
      display_preview_overlay_rect_ = result.preview_overlay_rect;
      setStatus(result.status);
      if (full_resolution) {
        last_full_preview_generation_ = result.generation;
        last_full_render_ms_ = result.total_elapsed_ms;
      } else {
        last_interactive_preview_generation_ = result.generation;
        last_interactive_render_ms_ = result.total_elapsed_ms;
      }
      last_decode_render_ms_ = result.decode_elapsed_ms;
      last_edit_render_ms_ = result.edit_elapsed_ms;
      last_encode_render_ms_ = result.encode_elapsed_ms;
      last_render_source_size_ = QSize(result.source_width, result.source_height);
      last_render_output_size_ = QSize(result.output_width, result.output_height);
      last_render_had_adjustments_ = result.had_adjustments;
    }
    refreshDebugInfo();
    emit previewChanged();

    const auto should_update_histogram =
        result.full_resolution ||
        (!full_render_timer_.isActive() && !full_request_pending_ && !full_render_watcher_.isRunning());
    if (result.success && !result.histogram_image.isNull() && should_update_histogram) {
      startHistogramUpdate(result);
    }
  }

  auto& pending_request = full_resolution ? pending_full_request_ : pending_interactive_request_;
  auto& pending = full_resolution ? full_request_pending_ : interactive_request_pending_;
  if (pending) {
    pending = false;
    startRenderStage(full_resolution, pending_request);
    return;
  }

  if (!full_resolution && result.draft_interactive && full_request_pending_ &&
      !interactive_render_timer_.isActive()) {
    next_interactive_render_draft_ = false;
    startRenderStage(false, currentRenderRequest(false));
    return;
  }

  if (!full_resolution && full_request_pending_ && !full_render_watcher_.isRunning()) {
    full_request_pending_ = false;
    startRenderStage(true, pending_full_request_);
    return;
  }

  updateProcessingState();
}

void EditorPreviewController::startRenderStage(bool full_resolution, const RenderRequest& request) {
  auto& watcher = full_resolution ? full_render_watcher_ : interactive_render_watcher_;
  auto queued_request = request;
  queued_request.queue_wait_ms =
      queued_request.requested_at_msecs > 0
          ? qMax<qint64>(0, QDateTime::currentMSecsSinceEpoch() - queued_request.requested_at_msecs)
          : 0;
  observability::logInfo(
      photoEditorPreviewLog(),
      QStringLiteral("preview.render.stage_queued"),
      {
          {QStringLiteral("stage"), full_resolution ? QStringLiteral("full") : QStringLiteral("interactive")},
          {QStringLiteral("file_path"), queued_request.file_path},
          {QStringLiteral("generation"), queued_request.generation},
          {QStringLiteral("render_reason"), queued_request.render_reason},
          {QStringLiteral("interaction_mode"), queued_request.interaction_mode},
          {QStringLiteral("queue_wait_ms"), queued_request.queue_wait_ms},
      });
  watcher.setFuture(QtConcurrent::run([queued_request]() { return render_preview_request(queued_request); }));
  updateProcessingState();
}

void EditorPreviewController::updateProcessingState() {
  const auto busy = interactive_render_timer_.isActive() ||
                    full_render_timer_.isActive() ||
                    interactive_render_watcher_.isRunning() ||
                    full_render_watcher_.isRunning() ||
                    histogram_watcher_.isRunning() ||
                    interactive_request_pending_ ||
                    full_request_pending_ ||
                    histogram_update_pending_;
  setProcessing(busy);
}

RenderRequest EditorPreviewController::currentRenderRequest(bool full_resolution) const {
  auto state = currentState();
  if (retouch_before_after_enabled_) {
    state.mask_stack = retouch_before_after_mask_stack(state.mask_stack);
  }
  const auto draft_interactive = !full_resolution && next_interactive_render_draft_;
  const auto max_edge =
      full_resolution ? 0
                      : (draft_interactive
                             ? draft_interactive_preview_edge_for_state(state, previewTargetEdge())
                             : interactive_preview_edge_for_state(state, previewTargetEdge()));
  const auto interaction_mode = interaction_mode_for_request(full_resolution, draft_interactive);
  return RenderRequest{
      .file_path = file_path_,
      .reference_file_path = reference_image_path_,
      .fallback_thumbnail_source = fallback_thumbnail_source_,
      .render_reason = render_reason_,
      .interaction_mode = interaction_mode,
      .state = state,
      .compare_adjustment_groups = compare_adjustment_groups_,
      .compare_mode = compare_mode_,
      .bypass_crop = crop_editing_,
      .full_resolution = full_resolution,
      .max_edge = max_edge,
      .jpeg_quality = full_resolution ? 95 : (uses_expensive_interactive_preview(state) ? 86 : 90),
      .generation = render_generation_,
      .requested_at_msecs = render_requested_at_msecs_,
      .use_in_memory_preview = !full_resolution && image_provider_ != nullptr,
      .preview_overlay_rect =
          !full_resolution && image_provider_ != nullptr && preview_viewport_active_ && !compare_mode_
              ? preview_viewport_rect_
              : QRectF(),
      .preview_overlay_max_edge =
          !full_resolution && image_provider_ != nullptr && preview_viewport_active_ && !compare_mode_
              ? preview_viewport_patch_max_edge_
              : 0,
      .draft_interactive = draft_interactive,
      .latest_generation = latest_render_generation_,
  };
}

void EditorPreviewController::refreshDebugInfo() {
  const QFileInfo source_info(file_path_);
  const auto info = QStringLiteral(
                        "status=%1 | file=%2 | exists=%3 | readable=%4 | output=%5 | thumb=%6 | preview=%7 | display=%8 | "
                        "reference=%9 | displayReference=%10 | renderMs(i=%11,f=%12,d=%13,e=%14,w=%15) | "
                        "source=%16x%17 | output=%18x%19 | adjusted=%20")
                        .arg(status_)
                        .arg(file_path_.isEmpty() ? QStringLiteral("<none>") : file_path_)
                        .arg(source_info.exists() ? QStringLiteral("yes") : QStringLiteral("no"))
                        .arg(source_info.isReadable() ? QStringLiteral("yes") : QStringLiteral("no"))
                        .arg(output_color_space_)
                        .arg(fallback_thumbnail_source_.isEmpty() ? QStringLiteral("<none>") : fallback_thumbnail_source_)
                        .arg(preview_source_.isEmpty() ? QStringLiteral("<none>") : preview_source_)
                        .arg(display_preview_source_.isEmpty() ? QStringLiteral("<none>") : display_preview_source_)
                        .arg(reference_preview_source_.isEmpty() ? QStringLiteral("<none>") : reference_preview_source_)
                        .arg(display_reference_preview_source_.isEmpty() ? QStringLiteral("<none>") : display_reference_preview_source_)
                        .arg(last_interactive_render_ms_)
                        .arg(last_full_render_ms_)
                        .arg(last_decode_render_ms_)
                        .arg(last_edit_render_ms_)
                        .arg(last_encode_render_ms_)
                        .arg(last_render_source_size_.width())
                        .arg(last_render_source_size_.height())
                        .arg(last_render_output_size_.width())
                        .arg(last_render_output_size_.height())
                        .arg(last_render_had_adjustments_ ? QStringLiteral("yes") : QStringLiteral("no"));
  if (debug_info_ == info) {
    return;
  }
  debug_info_ = info;
  emit debugInfoChanged();
}

void EditorPreviewController::setStatus(const QString& status) {
  if (status_ == status) {
    return;
  }
  status_ = status;
  emit statusChanged();
}

void EditorPreviewController::setProcessing(bool processing) {
  if (processing_ == processing) {
    return;
  }
  processing_ = processing;
  emit processingChanged();
}

QString EditorPreviewController::cachePathForSelection(bool edited, bool full_resolution) const {
  const auto state = currentState();
  const auto max_edge = full_resolution ? 0 : interactive_preview_edge_for_state(state, previewTargetEdge());
  return cache_path_for_selection(file_path_, state, edited, full_resolution, crop_editing_, max_edge);
}

QVector<EditState>& EditorPreviewController::undoStackForAsset(const QString& asset_id) {
  return asset_undo_states_[asset_id];
}

QVector<EditState>& EditorPreviewController::redoStackForAsset(const QString& asset_id) {
  return asset_redo_states_[asset_id];
}

void EditorPreviewController::pushUndoState(const EditState& state) {
  if (asset_id_.isEmpty()) {
    return;
  }

  auto& undo_stack = undoStackForAsset(asset_id_);
  if (!undo_stack.isEmpty() && edit_states_equal(undo_stack.back(), state)) {
    return;
  }

  undo_stack.push_back(state);
  if (undo_stack.size() > kMaxEditHistoryDepth) {
    undo_stack.removeFirst();
  }
}

void EditorPreviewController::rememberStateForUndo() {
  if (asset_id_.isEmpty() || applying_history_state_) {
    return;
  }

  pushUndoState(currentState());
  redoStackForAsset(asset_id_).clear();
  emit historyChanged();
}

void EditorPreviewController::clearHistoryForAsset(const QString& asset_id) {
  if (asset_id.isEmpty()) {
    return;
  }

  asset_undo_states_.remove(asset_id);
  asset_redo_states_.remove(asset_id);
}

void EditorPreviewController::applyHistoryState(const EditState& state) {
  applying_history_state_ = true;
  applyState(state);
  persistCurrentState();
  applying_history_state_ = false;
  emit historyChanged();
  renderPreview();
}

void EditorPreviewController::persistCurrentState() {
  if (asset_id_.isEmpty()) {
    return;
  }
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE")) {
    asset_states_.insert(asset_id_, currentState());
    return;
  }
  asset_states_.insert(asset_id_, currentState());
  persist_timer_.start();
}

void EditorPreviewController::restoreStateForAsset(const QString& asset_id, const QString& recipe_json) {
  if (asset_id.isEmpty()) {
    applyState(EditState{});
    emit historyChanged();
    return;
  }

  // Track access order and prune oldest assets beyond limit
  constexpr int kMaxTrackedAssets = 20;
  asset_access_order_.removeOne(asset_id);
  asset_access_order_.prepend(asset_id);
  while (asset_access_order_.size() > kMaxTrackedAssets) {
    const auto& evict_id = asset_access_order_.last();
    asset_states_.remove(evict_id);
    asset_undo_states_.remove(evict_id);
    asset_redo_states_.remove(evict_id);
    asset_access_order_.removeLast();
  }

  if (asset_states_.contains(asset_id)) {
    applyState(asset_states_.value(asset_id));
    emit historyChanged();
    return;
  }

  clearHistoryForAsset(asset_id);
  auto restored_state = stateFromRecipeJson(recipe_json);
  if (recipe_json.trimmed().isEmpty()) {
    const auto default_key = normalizedCameraDefaultKey(camera_label_);
    const auto default_iterator = camera_default_states_.constFind(default_key);
    if (!default_key.isEmpty() && default_iterator != camera_default_states_.cend()) {
      restored_state = merge_edit_recipe_state_by_groups(
          restored_state,
          default_iterator.value(),
          {QStringLiteral("raw"), QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")});
    }
    apply_matched_lens_profile(&restored_state, camera_label_, lens_model_);
  }
  applyState(restored_state);
  emit historyChanged();
}

void EditorPreviewController::refreshRawAsShotWhiteBalanceMetadata() {
  setRawAsShotWhiteBalance(5200.0, 0.0);
  if (!raw_image_supported(file_path_)) {
    return;
  }

  photo_editor::pipeline::DecodedImageMetadata metadata;
  auto settings = raw_decode_settings_for_state(currentState());
  settings.white_balance_override_enabled = false;
  if (read_raw_decoded_metadata(file_path_, &metadata, settings)) {
    setRawAsShotWhiteBalance(metadata.as_shot_temperature, metadata.as_shot_tint);
  }
}

void EditorPreviewController::setRawAsShotWhiteBalance(double temperature, double tint) {
  raw_as_shot_white_balance_temperature_ = qBound(2000.0, temperature, 50000.0);
  raw_as_shot_white_balance_tint_ = qBound(-150.0, tint, 150.0);
}

void EditorPreviewController::applyState(const EditState& state) {
  raw_exposure_ = state.raw_exposure;
  raw_boost_ = state.raw_boost;
  raw_shadow_boost_ = state.raw_shadow_boost;
  raw_detail_ = state.raw_detail;
  raw_chroma_noise_reduction_ = std::clamp(state.raw_chroma_noise_reduction, 0.0, 1.0);
  raw_moire_reduction_ = std::clamp(state.raw_moire_reduction, 0.0, 1.0);
  raw_hot_dead_pixel_cleanup_ = std::clamp(state.raw_hot_dead_pixel_cleanup, 0.0, 1.0);
  raw_false_color_suppression_ = std::clamp(state.raw_false_color_suppression, 0.0, 1.0);
  raw_highlight_recovery_ = state.raw_highlight_recovery;
  raw_demosaic_quality_ = normalized_raw_demosaic_quality_name(state.raw_demosaic_quality);
  raw_gamut_mapping_ = state.raw_gamut_mapping;
  raw_lens_correction_ = state.raw_lens_correction;
  raw_white_balance_override_ = state.raw_white_balance_override;
  raw_camera_matching_look_ = normalized_camera_matching_look_name(state.raw_camera_matching_look);
  raw_white_balance_temperature_ = state.raw_white_balance_temperature;
  raw_white_balance_tint_ = state.raw_white_balance_tint;
  raw_black_level_ = std::clamp(state.raw_black_level, 0.0, 0.95);
  raw_white_level_ = std::clamp(state.raw_white_level, raw_black_level_ + 0.01, 4.0);
  raw_calibration_hue_ = clamp_signed_percent(state.raw_calibration_hue);
  raw_calibration_saturation_ = clamp_signed_percent(state.raw_calibration_saturation);
  raw_red_primary_hue_ = clamp_signed_percent(state.raw_red_primary_hue);
  raw_red_primary_saturation_ = clamp_signed_percent(state.raw_red_primary_saturation);
  raw_green_primary_hue_ = clamp_signed_percent(state.raw_green_primary_hue);
  raw_green_primary_saturation_ = clamp_signed_percent(state.raw_green_primary_saturation);
  raw_blue_primary_hue_ = clamp_signed_percent(state.raw_blue_primary_hue);
  raw_blue_primary_saturation_ = clamp_signed_percent(state.raw_blue_primary_saturation);
  exposure_ = state.exposure;
  contrast_ = state.contrast;
  highlights_ = state.highlights;
  shadows_ = state.shadows;
  whites_ = state.whites;
  blacks_ = state.blacks;
  brightness_ = clamp_signed_percent(state.brightness);
  hdr_local_tone_mapping_ = qBound(0.0, state.hdr_local_tone_mapping, 100.0);
  filmic_shoulder_ = clamp_signed_percent(state.filmic_shoulder);
  filmic_toe_ = clamp_signed_percent(state.filmic_toe);
  adaptive_contrast_ = clamp_signed_percent(state.adaptive_contrast);
  levels_black_point_ = qBound(0.0, state.levels_black_point, 100.0);
  levels_gamma_ = clamp_signed_percent(state.levels_gamma);
  levels_white_point_ = qBound(0.0, state.levels_white_point, 100.0);
  zone_deep_shadows_ = clamp_signed_percent(state.zone_deep_shadows);
  zone_shadows_ = clamp_signed_percent(state.zone_shadows);
  zone_midtones_ = clamp_signed_percent(state.zone_midtones);
  zone_lights_ = clamp_signed_percent(state.zone_lights);
  zone_highlights_ = clamp_signed_percent(state.zone_highlights);
  temperature_ = state.temperature;
  tint_ = state.tint;
  texture_ = state.texture;
  clarity_ = state.clarity;
  dehaze_ = state.dehaze;
  sharpening_ = state.sharpening;
  sharpen_masking_ = state.sharpen_masking;
  luminance_nr_ = state.luminance_nr;
  color_nr_ = state.color_nr;
  lens_distortion_ = state.lens_distortion;
  lens_vignette_ = state.lens_vignette;
  chromatic_aberration_ = state.chromatic_aberration;
  perspective_vertical_ = state.perspective_vertical;
  perspective_horizontal_ = state.perspective_horizontal;
  transform_rotation_ = qBound(-45.0, state.transform_rotation, 45.0);
  transform_scale_ = qBound(0.25, state.transform_scale, 4.0);
  transform_skew_x_ = clamp_signed_percent(state.transform_skew_x);
  transform_skew_y_ = clamp_signed_percent(state.transform_skew_y);
  liquify_center_x_ = qBound(0.0, state.liquify_center_x, 1.0);
  liquify_center_y_ = qBound(0.0, state.liquify_center_y, 1.0);
  liquify_radius_ = qBound(0.02, state.liquify_radius, 1.0);
  liquify_strength_ = clamp_signed_percent(state.liquify_strength);
  content_aware_crop_fill_ = state.content_aware_crop_fill;
  vibrance_ = state.vibrance;
  saturation_ = state.saturation;
  curve_shadows_ = state.curve_shadows;
  curve_darks_ = state.curve_darks;
  curve_lights_ = state.curve_lights;
  curve_highlights_ = state.curve_highlights;
  master_curve_points_ = state.master_curve_points;
  luma_curve_points_ = state.luma_curve_points;
  red_curve_points_ = state.red_curve_points;
  green_curve_points_ = state.green_curve_points;
  blue_curve_points_ = state.blue_curve_points;
  color_mixer_model_ = state.color_mixer_model.isEmpty() ? QStringLiteral("OKLab") : state.color_mixer_model;
  hsl_channel_ = state.hsl_channel;
  hue_adjustment_ = state.hue_adjustment;
  saturation_adjustment_ = state.saturation_adjustment;
  luminance_adjustment_ = state.luminance_adjustment;
  for (std::size_t i = 0; i < 8; ++i) {
    mixer_channels_[i].hue = state.mixer_channels[i].hue;
    mixer_channels_[i].saturation = state.mixer_channels[i].saturation;
    mixer_channels_[i].luminance = state.mixer_channels[i].luminance;
    mixer_channels_[i].selective_cyan = state.mixer_channels[i].selective_cyan;
    mixer_channels_[i].selective_magenta = state.mixer_channels[i].selective_magenta;
    mixer_channels_[i].selective_yellow = state.mixer_channels[i].selective_yellow;
    mixer_channels_[i].selective_black = state.mixer_channels[i].selective_black;
  }
  rgb_channel_mixer_ = state.rgb_channel_mixer;
  black_and_white_enabled_ = state.black_and_white_enabled;
  black_and_white_mixer_ = state.black_and_white_mixer;
  color_replacement_enabled_ = state.color_replacement_enabled;
  color_replacement_source_hue_ = state.color_replacement_source_hue;
  color_replacement_target_hue_ = state.color_replacement_target_hue;
  color_replacement_range_ = state.color_replacement_range;
  color_replacement_strength_ = state.color_replacement_strength;
  skin_tone_protection_enabled_ = state.skin_tone_protection_enabled;
  skin_tone_line_hue_ = state.skin_tone_line_hue;
  skin_tone_protection_range_ = state.skin_tone_protection_range;
  skin_tone_protection_strength_ = state.skin_tone_protection_strength;
  color_harmony_enabled_ = state.color_harmony_enabled;
  color_harmony_mode_ = state.color_harmony_mode.isEmpty() ? QStringLiteral("Complementary") : state.color_harmony_mode;
  color_harmony_base_hue_ = state.color_harmony_base_hue;
  color_harmony_strength_ = state.color_harmony_strength;
  grading_shadows_hue_ = state.grading_shadows_hue;
  grading_shadows_saturation_ = state.grading_shadows_saturation;
  grading_shadows_luminance_ = state.grading_shadows_luminance;
  grading_midtones_hue_ = state.grading_midtones_hue;
  grading_midtones_saturation_ = state.grading_midtones_saturation;
  grading_midtones_luminance_ = state.grading_midtones_luminance;
  grading_highlights_hue_ = state.grading_highlights_hue;
  grading_highlights_saturation_ = state.grading_highlights_saturation;
  grading_highlights_luminance_ = state.grading_highlights_luminance;
  crop_rect_x_ = state.crop_rect_x;
  crop_rect_y_ = state.crop_rect_y;
  crop_rect_width_ = state.crop_rect_width;
  crop_rect_height_ = state.crop_rect_height;
  crop_zoom_ = state.crop_zoom;
  crop_preset_ = state.crop_preset;
  upright_mode_ = normalized_upright_mode_name(state.upright_mode);
  upright_vertical_guide_angle_ = qBound(-45.0, state.upright_vertical_guide_angle, 45.0);
  upright_horizontal_guide_angle_ = qBound(-45.0, state.upright_horizontal_guide_angle, 45.0);
  input_color_profile_ = normalized_input_color_profile_name(state.input_color_profile);
  working_color_profile_ = normalized_working_color_profile_name(state.working_color_profile);
  preview_color_space_ = normalized_output_color_space_name(state.preview_color_space);
  monitor_color_profile_ = normalized_monitor_color_profile_name(state.monitor_color_profile);
  soft_proof_enabled_ = state.soft_proof_enabled;
  soft_proof_profile_ = normalized_output_color_space_name(state.soft_proof_profile);
  render_intent_ = normalized_render_intent_name(state.render_intent);
  raw_input_transform_enabled_ = state.raw_input_transform_enabled;
  highlight_reconstruction_mode_ =
      normalized_highlight_reconstruction_mode_name(state.highlight_reconstruction_mode);
  chromatic_adaptation_method_ =
      normalized_chromatic_adaptation_method_name(state.chromatic_adaptation_method);
  output_color_space_ = normalized_output_color_space_name(state.output_color_space);
  mask_stack_ = normalize_mask_stack(state.mask_stack);
  photo_layers_ = normalized_photo_layers(state.layers);
  invalidateMaskDerivedCaches();
  active_mask_index_ =
      mask_stack_.isEmpty() || state.active_mask_index < 0
          ? -1
          : qBound(0, state.active_mask_index, static_cast<int>(mask_stack_.size()) - 1);
  synchronizeMaskState();
  emit controlsChanged();
}

EditState EditorPreviewController::currentState() const {
  return EditState{
      .input_color_profile = input_color_profile_,
      .working_color_profile = working_color_profile_,
      .preview_color_space = preview_color_space_,
      .monitor_color_profile = monitor_color_profile_,
      .soft_proof_enabled = soft_proof_enabled_,
      .soft_proof_profile = soft_proof_profile_,
      .render_intent = render_intent_,
      .raw_input_transform_enabled = raw_input_transform_enabled_,
      .highlight_reconstruction_mode = highlight_reconstruction_mode_,
      .chromatic_adaptation_method = chromatic_adaptation_method_,
      .raw_exposure = raw_exposure_,
      .raw_boost = raw_boost_,
      .raw_shadow_boost = raw_shadow_boost_,
      .raw_detail = raw_detail_,
      .raw_chroma_noise_reduction = raw_chroma_noise_reduction_,
      .raw_moire_reduction = raw_moire_reduction_,
      .raw_hot_dead_pixel_cleanup = raw_hot_dead_pixel_cleanup_,
      .raw_false_color_suppression = raw_false_color_suppression_,
      .raw_highlight_recovery = raw_highlight_recovery_,
      .raw_demosaic_quality = raw_demosaic_quality_,
      .raw_gamut_mapping = raw_gamut_mapping_,
      .raw_lens_correction = raw_lens_correction_,
      .raw_white_balance_override = raw_white_balance_override_,
      .raw_camera_matching_look = raw_camera_matching_look_,
      .raw_white_balance_temperature = raw_white_balance_temperature_,
      .raw_white_balance_tint = raw_white_balance_tint_,
      .raw_black_level = raw_black_level_,
      .raw_white_level = raw_white_level_,
      .raw_calibration_hue = raw_calibration_hue_,
      .raw_calibration_saturation = raw_calibration_saturation_,
      .raw_red_primary_hue = raw_red_primary_hue_,
      .raw_red_primary_saturation = raw_red_primary_saturation_,
      .raw_green_primary_hue = raw_green_primary_hue_,
      .raw_green_primary_saturation = raw_green_primary_saturation_,
      .raw_blue_primary_hue = raw_blue_primary_hue_,
      .raw_blue_primary_saturation = raw_blue_primary_saturation_,
      .exposure = exposure_,
      .contrast = contrast_,
      .highlights = highlights_,
      .shadows = shadows_,
      .whites = whites_,
      .blacks = blacks_,
      .brightness = brightness_,
      .hdr_local_tone_mapping = hdr_local_tone_mapping_,
      .filmic_shoulder = filmic_shoulder_,
      .filmic_toe = filmic_toe_,
      .adaptive_contrast = adaptive_contrast_,
      .levels_black_point = levels_black_point_,
      .levels_gamma = levels_gamma_,
      .levels_white_point = levels_white_point_,
      .zone_deep_shadows = zone_deep_shadows_,
      .zone_shadows = zone_shadows_,
      .zone_midtones = zone_midtones_,
      .zone_lights = zone_lights_,
      .zone_highlights = zone_highlights_,
      .temperature = temperature_,
      .tint = tint_,
      .texture = texture_,
      .clarity = clarity_,
      .dehaze = dehaze_,
      .sharpening = sharpening_,
      .sharpen_masking = sharpen_masking_,
      .luminance_nr = luminance_nr_,
      .color_nr = color_nr_,
      .lens_distortion = lens_distortion_,
      .lens_vignette = lens_vignette_,
      .chromatic_aberration = chromatic_aberration_,
      .perspective_vertical = perspective_vertical_,
      .perspective_horizontal = perspective_horizontal_,
      .transform_rotation = transform_rotation_,
      .transform_scale = transform_scale_,
      .transform_skew_x = transform_skew_x_,
      .transform_skew_y = transform_skew_y_,
      .liquify_center_x = liquify_center_x_,
      .liquify_center_y = liquify_center_y_,
      .liquify_radius = liquify_radius_,
      .liquify_strength = liquify_strength_,
      .content_aware_crop_fill = content_aware_crop_fill_,
      .vibrance = vibrance_,
      .saturation = saturation_,
      .curve_shadows = curve_shadows_,
      .curve_darks = curve_darks_,
      .curve_lights = curve_lights_,
      .curve_highlights = curve_highlights_,
      .master_curve_points = master_curve_points_,
      .luma_curve_points = luma_curve_points_,
      .red_curve_points = red_curve_points_,
      .green_curve_points = green_curve_points_,
      .blue_curve_points = blue_curve_points_,
      .color_mixer_model = color_mixer_model_,
      .hsl_channel = hsl_channel_,
      .hue_adjustment = hue_adjustment_,
      .saturation_adjustment = saturation_adjustment_,
      .luminance_adjustment = luminance_adjustment_,
      .mixer_channels = [this]() {
        std::array<EditRecipeState::MixerChannel, 8> channels{};
        for (std::size_t i = 0; i < 8; ++i) {
          channels[i].hue = mixer_channels_[i].hue;
          channels[i].saturation = mixer_channels_[i].saturation;
          channels[i].luminance = mixer_channels_[i].luminance;
          channels[i].selective_cyan = mixer_channels_[i].selective_cyan;
          channels[i].selective_magenta = mixer_channels_[i].selective_magenta;
          channels[i].selective_yellow = mixer_channels_[i].selective_yellow;
          channels[i].selective_black = mixer_channels_[i].selective_black;
        }
        return channels;
      }(),
      .rgb_channel_mixer = rgb_channel_mixer_,
      .black_and_white_enabled = black_and_white_enabled_,
      .black_and_white_mixer = black_and_white_mixer_,
      .color_replacement_enabled = color_replacement_enabled_,
      .color_replacement_source_hue = color_replacement_source_hue_,
      .color_replacement_target_hue = color_replacement_target_hue_,
      .color_replacement_range = color_replacement_range_,
      .color_replacement_strength = color_replacement_strength_,
      .skin_tone_protection_enabled = skin_tone_protection_enabled_,
      .skin_tone_line_hue = skin_tone_line_hue_,
      .skin_tone_protection_range = skin_tone_protection_range_,
      .skin_tone_protection_strength = skin_tone_protection_strength_,
      .color_harmony_enabled = color_harmony_enabled_,
      .color_harmony_mode = color_harmony_mode_,
      .color_harmony_base_hue = color_harmony_base_hue_,
      .color_harmony_strength = color_harmony_strength_,
      .grading_shadows_hue = grading_shadows_hue_,
      .grading_shadows_saturation = grading_shadows_saturation_,
      .grading_shadows_luminance = grading_shadows_luminance_,
      .grading_midtones_hue = grading_midtones_hue_,
      .grading_midtones_saturation = grading_midtones_saturation_,
      .grading_midtones_luminance = grading_midtones_luminance_,
      .grading_highlights_hue = grading_highlights_hue_,
      .grading_highlights_saturation = grading_highlights_saturation_,
      .grading_highlights_luminance = grading_highlights_luminance_,
      .crop_rect_x = crop_rect_x_,
      .crop_rect_y = crop_rect_y_,
      .crop_rect_width = crop_rect_width_,
      .crop_rect_height = crop_rect_height_,
      .crop_zoom = crop_zoom_,
      .crop_preset = crop_preset_,
      .upright_mode = upright_mode_,
      .upright_vertical_guide_angle = upright_vertical_guide_angle_,
      .upright_horizontal_guide_angle = upright_horizontal_guide_angle_,
      .output_color_space = output_color_space_,
      .linear_mask_enabled = linear_mask_enabled_,
      .linear_mask_exposure = linear_mask_exposure_,
      .linear_mask_temperature = linear_mask_temperature_,
      .linear_mask_feather = linear_mask_feather_,
      .linear_mask_position = linear_mask_position_,
      .linear_mask_rotation = linear_mask_rotation_,
      .radial_mask_enabled = radial_mask_enabled_,
      .radial_mask_exposure = radial_mask_exposure_,
      .radial_mask_temperature = radial_mask_temperature_,
      .radial_mask_feather = radial_mask_feather_,
      .radial_mask_center_x = radial_mask_center_x_,
      .radial_mask_center_y = radial_mask_center_y_,
      .radial_mask_radius_x = radial_mask_radius_x_,
      .radial_mask_radius_y = radial_mask_radius_y_,
      .radial_mask_rotation = radial_mask_rotation_,
      .mask_stack = mask_stack_,
      .active_mask_index = active_mask_index_,
      .layers = photo_layers_,
  };
}

EditState EditorPreviewController::stateFromRecipeJson(const QString& recipe_json) const {
  return parse_edit_recipe_json(recipe_json);
}

QString EditorPreviewController::recipeJsonForState(const EditState& state) const {
  return serialize_edit_recipe_json(state);
}

QString EditorPreviewController::normalizedPresetName(const QString& name) const {
  return name.simplified();
}

QString EditorPreviewController::normalizedCameraDefaultKey(const QString& camera_label) const {
  return camera_label.simplified().toLower();
}

int EditorPreviewController::firstMaskIndexForKind(const QString& kind) const {
  const auto normalized_kind = normalized_mask_kind(kind);
  for (auto index = 0; index < mask_stack_.size(); ++index) {
    const auto mask = mask_stack_.at(index).toMap();
    if (mask.value(QStringLiteral("kind")).toString() == normalized_kind) {
      return index;
    }
  }
  return -1;
}

QVariantMap EditorPreviewController::activeMaskDefinition() const {
  if (!hasActiveMask()) {
    return {};
  }
  if (active_mask_cache_dirty_) {
    cached_active_mask_definition_ = mask_stack_.at(active_mask_index_).toMap();
    active_mask_cache_dirty_ = false;
  }
  return cached_active_mask_definition_;
}

QVariantMap EditorPreviewController::maskDefinitionAt(int index) const {
  if (index < 0 || index >= mask_stack_.size()) {
    return {};
  }
  return mask_stack_.at(index).toMap();
}

void EditorPreviewController::invalidateActiveMaskCache() {
  active_mask_cache_dirty_ = true;
  cached_active_mask_definition_.clear();
}

void EditorPreviewController::invalidateMaskDerivedCaches() {
  mask_group_cache_dirty_ = true;
  cached_mask_sets_.clear();
  cached_retouch_stack_.clear();
  invalidateActiveMaskCache();
}

void EditorPreviewController::synchronizeMaskState() {
  if (active_mask_index_ >= static_cast<int>(mask_stack_.size())) {
    active_mask_index_ = mask_stack_.isEmpty() ? -1 : static_cast<int>(mask_stack_.size()) - 1;
  }

  const auto linear_index = firstMaskIndexForKind(QStringLiteral("linear"));
  if (linear_index >= 0) {
    const auto linear_mask = maskDefinitionAt(linear_index);
    linear_mask_enabled_ = map_bool_value(linear_mask, QStringLiteral("enabled"), false);
    linear_mask_exposure_ = map_double_value(linear_mask, QStringLiteral("exposure"), 0.0);
    linear_mask_temperature_ = map_double_value(linear_mask, QStringLiteral("temperature"), 0.0);
    linear_mask_feather_ = map_double_value(linear_mask, QStringLiteral("feather"), 0.22);
    linear_mask_position_ = map_double_value(linear_mask, QStringLiteral("position"), 0.5);
    linear_mask_rotation_ = map_double_value(linear_mask, QStringLiteral("rotation"), 0.0);
  } else {
    linear_mask_enabled_ = false;
    linear_mask_exposure_ = 0.0;
    linear_mask_temperature_ = 0.0;
    linear_mask_feather_ = 0.22;
    linear_mask_position_ = 0.5;
    linear_mask_rotation_ = 0.0;
  }

  const auto radial_index = firstMaskIndexForKind(QStringLiteral("radial"));
  if (radial_index >= 0) {
    const auto radial_mask = maskDefinitionAt(radial_index);
    radial_mask_enabled_ = map_bool_value(radial_mask, QStringLiteral("enabled"), false);
    radial_mask_exposure_ = map_double_value(radial_mask, QStringLiteral("exposure"), 0.0);
    radial_mask_temperature_ = map_double_value(radial_mask, QStringLiteral("temperature"), 0.0);
    radial_mask_feather_ = map_double_value(radial_mask, QStringLiteral("feather"), 0.25);
    radial_mask_center_x_ = map_double_value(radial_mask, QStringLiteral("centerX"), 0.5);
    radial_mask_center_y_ = map_double_value(radial_mask, QStringLiteral("centerY"), 0.5);
    radial_mask_radius_x_ = map_double_value(radial_mask, QStringLiteral("radiusX"), 0.25);
    radial_mask_radius_y_ = map_double_value(radial_mask, QStringLiteral("radiusY"), 0.25);
    radial_mask_rotation_ = map_double_value(radial_mask, QStringLiteral("rotation"), 0.0);
  } else {
    radial_mask_enabled_ = false;
    radial_mask_exposure_ = 0.0;
    radial_mask_temperature_ = 0.0;
    radial_mask_feather_ = 0.25;
    radial_mask_center_x_ = 0.5;
    radial_mask_center_y_ = 0.5;
    radial_mask_radius_x_ = 0.25;
    radial_mask_radius_y_ = 0.25;
    radial_mask_rotation_ = 0.0;
  }
}

bool EditorPreviewController::applyMaskStackChange(
    const QVariantList& stack,
    int active_index,
    bool rerender) {
  const auto normalized_stack = normalize_mask_stack(stack);
  const auto normalized_active_index =
      normalized_stack.isEmpty()
          ? -1
          : qBound(0, active_index, static_cast<int>(normalized_stack.size()) - 1);
  if (normalized_stack == mask_stack_ && normalized_active_index == active_mask_index_) {
    return false;
  }

  if (!brush_stroke_suspends_side_effects_) {
    rememberStateForUndo();
  }
  mask_stack_ = normalized_stack;
  invalidateMaskDerivedCaches();
  active_mask_index_ = normalized_active_index;
  synchronizeMaskState();
  if (!brush_stroke_suspends_side_effects_) {
    persistCurrentState();
  }
  emit controlsChanged();
  emit maskOverlayChanged();
  if (rerender) {
    renderPreview(true, QStringLiteral("mask_preview_change"));
  }
  return true;
}

bool EditorPreviewController::applyPhotoLayerStackChange(const QVariantList& layers) {
  const auto normalized_layers = normalized_photo_layers(layers);
  if (normalized_layers == photo_layers_) {
    return false;
  }
  rememberStateForUndo();
  photo_layers_ = normalized_layers;
  persistCurrentState();
  emit controlsChanged();
  renderPreview(false, QStringLiteral("photo_layer_change"));
  return true;
}

bool EditorPreviewController::updateMaskAt(
    int index,
    const QVariantMap& mask,
    bool rerender,
    bool allow_locked) {
  if (index < 0 || index >= mask_stack_.size()) {
    return false;
  }
  const auto existing_mask = mask_stack_.at(index).toMap();
  if (!allow_locked && map_bool_value(existing_mask, QStringLiteral("locked"), false)) {
    return false;
  }
  auto next_stack = mask_stack_;
  const auto normalized_mask = normalized_mask_definition(mask, index + 1);
  next_stack[index] = normalized_mask;

  if (is_selection_mask(normalized_mask)) {
    const auto set_id = normalized_mask.value(QStringLiteral("maskSetId")).toString();
    for (int sibling_index = 0; sibling_index < next_stack.size(); ++sibling_index) {
      if (sibling_index == index) {
        continue;
      }
      auto sibling_mask = next_stack.at(sibling_index).toMap();
      if (!is_selection_mask(sibling_mask) ||
          sibling_mask.value(QStringLiteral("maskSetId")).toString() != set_id) {
        continue;
      }
      copy_selection_adjustments_to_mask(&sibling_mask, normalized_mask);
      next_stack[sibling_index] = sibling_mask;
    }
  }
  return applyMaskStackChange(next_stack, active_mask_index_, rerender);
}

bool EditorPreviewController::updateActiveMaskDefinition(
    const QVariantMap& mask,
    bool rerender,
    bool allow_locked) {
  return updateMaskAt(active_mask_index_, mask, rerender, allow_locked);
}

bool EditorPreviewController::updateLegacyKindMask(const QString& kind, const QVariantMap& updates) {
  const auto normalized_kind = normalized_mask_kind(kind);
  auto index = firstMaskIndexForKind(normalized_kind);
  auto next_stack = mask_stack_;
  QVariantMap mask;
  if (index >= 0) {
    mask = next_stack.at(index).toMap();
  } else {
    index = static_cast<int>(next_stack.size());
    mask = default_mask_definition(normalized_kind, index + 1);
    next_stack.append(mask);
  }

  for (auto it = updates.cbegin(); it != updates.cend(); ++it) {
    mask.insert(it.key(), it.value());
  }
  next_stack[index] = normalized_mask_definition(mask, index + 1);
  return applyMaskStackChange(next_stack, index);
}

QString EditorPreviewController::presetsFilePath() const {
  return QDir(AppBootstrap::appDataLocation()).filePath(QStringLiteral("editor-presets.json"));
}

QString EditorPreviewController::cameraDefaultsFilePath() const {
  return QDir(AppBootstrap::appDataLocation()).filePath(QStringLiteral("camera-defaults.json"));
}

QString EditorPreviewController::customCropPresetsFilePath() const {
  return QDir(AppBootstrap::appDataLocation()).filePath(QStringLiteral("custom-crop-presets.json"));
}

void EditorPreviewController::loadCustomCropPresets() {
  custom_crop_presets_.clear();
  QFile file(customCropPresetsFilePath());
  if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
    return;
  }
  const auto document = QJsonDocument::fromJson(file.readAll());
  if (!document.isObject()) {
    return;
  }
  const auto preset_array = document.object().value(QStringLiteral("presets")).toArray();
  for (const auto& value : preset_array) {
    const auto normalized = normalized_custom_crop_preset(value.toString());
    if (!normalized.isEmpty() &&
        !built_in_crop_aspect_presets().contains(normalized, Qt::CaseInsensitive) &&
        !custom_crop_presets_.contains(normalized, Qt::CaseInsensitive)) {
      custom_crop_presets_.push_back(normalized);
    }
  }
}

void EditorPreviewController::saveCustomCropPresets() const {
  const QFileInfo destination_info(customCropPresetsFilePath());
  QDir().mkpath(destination_info.dir().absolutePath());
  QJsonArray preset_array;
  for (const auto& preset : custom_crop_presets_) {
    preset_array.push_back(preset);
  }
  QJsonObject root;
  root.insert(QStringLiteral("presets"), preset_array);
  QSaveFile file(customCropPresetsFilePath());
  if (!file.open(QIODevice::WriteOnly)) {
    return;
  }
  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  file.commit();
}

void EditorPreviewController::loadPresets() {
  built_in_presets_ = built_in_preset_map();
  built_in_preset_packs_ = built_in_preset_pack_map();
  built_in_preset_categories_ = built_in_preset_category_map();
  built_in_preset_groups_ = built_in_preset_group_map();
  built_in_preset_adaptive_rules_ = built_in_preset_adaptive_rule_map();
  presets_.clear();
  user_preset_packs_.clear();
  user_preset_categories_.clear();
  user_preset_groups_.clear();
  user_preset_adaptive_rules_.clear();
  favorite_presets_.clear();

  QFile file(presetsFilePath());
  if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
    return;
  }

  const auto document = QJsonDocument::fromJson(file.readAll());
  if (!document.isObject()) {
    return;
  }

  const auto preset_array = document.object().value(QStringLiteral("presets")).toArray();
  for (const auto& preset_value : preset_array) {
    if (!preset_value.isObject()) {
      continue;
    }

    const auto preset_object = preset_value.toObject();
    const auto name = normalizedPresetName(preset_object.value(QStringLiteral("name")).toString());
    const auto recipe_json = preset_object.value(QStringLiteral("recipeJson")).toString();
    const auto pack = preset_object.value(QStringLiteral("pack")).toString(QStringLiteral("User Presets")).simplified();
    const auto category =
        normalized_preset_category(preset_object.value(QStringLiteral("category")).toString(), pack);
    const auto groups = preset_adjustment_groups_from_json(
        preset_object.value(QStringLiteral("adjustmentGroups")).toArray());
    if (name.isEmpty() || recipe_json.trimmed().isEmpty()) {
      continue;
    }

    const auto preset_state = stateFromRecipeJson(recipe_json);
    presets_.insert(name, preset_state);
    user_preset_packs_.insert(name, pack.isEmpty() ? QStringLiteral("User Presets") : pack);
    user_preset_categories_.insert(name, category);
    user_preset_groups_.insert(
        name,
        groups.isEmpty() ? declared_preset_groups_for_state(preset_state) : groups);
    const auto adaptive_rules = preset_object.value(QStringLiteral("adaptiveRules")).toArray();
    if (!adaptive_rules.isEmpty()) {
      user_preset_adaptive_rules_.insert(name, adaptive_rules);
    }
  }

  const auto favorite_array = document.object().value(QStringLiteral("favorites")).toArray();
  for (const auto& favorite_value : favorite_array) {
    const auto name = normalizedPresetName(favorite_value.toString());
    if (!name.isEmpty()) {
      favorite_presets_.insert(name);
    }
  }
}

void EditorPreviewController::savePresets() const {
  const QFileInfo destination_info(presetsFilePath());
  QDir().mkpath(destination_info.dir().absolutePath());

  QJsonArray preset_array;
  for (auto iterator = presets_.cbegin(); iterator != presets_.cend(); ++iterator) {
    QJsonObject preset_object;
    preset_object.insert(QStringLiteral("name"), iterator.key());
    preset_object.insert(
        QStringLiteral("pack"),
        user_preset_packs_.value(iterator.key(), QStringLiteral("User Presets")));
    preset_object.insert(
        QStringLiteral("category"),
        user_preset_categories_.value(
            iterator.key(),
            normalized_preset_category(
                user_preset_packs_.value(iterator.key(), QStringLiteral("User Presets")),
                QStringLiteral("User"))));
    preset_object.insert(
        QStringLiteral("adjustmentGroups"),
        preset_adjustment_groups_to_json(
            user_preset_groups_.value(iterator.key(), declared_preset_groups_for_state(iterator.value()))));
    const auto adaptive_rules = user_preset_adaptive_rules_.value(iterator.key());
    if (!adaptive_rules.isEmpty()) {
      preset_object.insert(QStringLiteral("adaptiveRules"), adaptive_rules);
    }
    preset_object.insert(QStringLiteral("recipeJson"), recipeJsonForState(iterator.value()));
    preset_array.push_back(preset_object);
  }

  QJsonArray favorite_array;
  for (const auto& name : favoritePresetNames()) {
    favorite_array.push_back(name);
  }

  QJsonObject root;
  root.insert(QStringLiteral("presets"), preset_array);
  root.insert(QStringLiteral("favorites"), favorite_array);

  QSaveFile file(presetsFilePath());
  if (!file.open(QIODevice::WriteOnly)) {
    return;
  }

  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  file.commit();
}

void EditorPreviewController::loadCameraDefaults() {
  camera_default_states_.clear();

  QFile file(cameraDefaultsFilePath());
  if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
    return;
  }

  const auto document = QJsonDocument::fromJson(file.readAll());
  if (!document.isObject()) {
    return;
  }

  const auto defaults = document.object().value(QStringLiteral("cameraDefaults")).toArray();
  for (const auto& value : defaults) {
    if (!value.isObject()) {
      continue;
    }
    const auto object = value.toObject();
    const auto key = normalizedCameraDefaultKey(object.value(QStringLiteral("cameraLabel")).toString());
    const auto recipe_json = object.value(QStringLiteral("recipeJson")).toString();
    if (key.isEmpty() || recipe_json.trimmed().isEmpty()) {
      continue;
    }
    camera_default_states_.insert(key, stateFromRecipeJson(recipe_json));
  }
}

void EditorPreviewController::saveCameraDefaults() const {
  const QFileInfo destination_info(cameraDefaultsFilePath());
  QDir().mkpath(destination_info.dir().absolutePath());

  QJsonArray defaults_array;
  for (auto iterator = camera_default_states_.cbegin(); iterator != camera_default_states_.cend(); ++iterator) {
    QJsonObject default_object;
    default_object.insert(QStringLiteral("cameraLabel"), iterator.key());
    default_object.insert(QStringLiteral("recipeJson"), recipeJsonForState(iterator.value()));
    defaults_array.push_back(default_object);
  }

  QJsonObject root;
  root.insert(QStringLiteral("cameraDefaults"), defaults_array);

  QSaveFile file(cameraDefaultsFilePath());
  if (!file.open(QIODevice::WriteOnly)) {
    return;
  }

  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  file.commit();
}

void EditorPreviewController::flushPersistedState() {
  if (asset_id_.isEmpty()) {
    return;
  }

  if (persist_process_.state() != QProcess::NotRunning) {
    persist_timer_.start();
    return;
  }

  const auto recipe_json = recipeJsonForState(currentState());
  persist_process_.setWorkingDirectory(QDir::currentPath());
  QProcessEnvironment environment;
  QString environment_error;
  if (!AppBootstrap::prepareProcessEnvironment(&environment, &environment_error)) {
    setStatus(environment_error.isEmpty() ? QStringLiteral("Edit recipe save is unavailable.") : environment_error);
    refreshDebugInfo();
    return;
  }
  persist_process_.setProcessEnvironment(environment);
  persist_process_.start(saveRecipeProgram(), saveRecipeArguments(asset_id_, recipe_json));
}

bool EditorPreviewController::hasVisibleAdjustments() const {
  return has_visible_adjustments(currentState());
}

bool EditorPreviewController::decodeSourceImage(QImage* image, int max_edge) const {
  return decode_source_image(file_path_, image, max_edge, currentState());
}

QSize EditorPreviewController::sourceImageSize() const {
  if (!preview_supported(file_path_)) {
    return {};
  }

  if (native_image_supported(file_path_)) {
    return native_image_size(file_path_);
  }

  QImageReader reader(file_path_);
  if (reader.canRead()) {
    return reader.size();
  }

  return raw_image_size(file_path_);
}

QImage EditorPreviewController::applyCrop(const QImage& image) const {
  return apply_crop(image, currentState());
}

void EditorPreviewController::applyColorAdjustments(QImage* image) const {
  apply_color_adjustments(image, currentState());
}

void EditorPreviewController::updateRawClippingWarnings(const QImage& image) {
  const auto raw_clipping = raw_clipping_stats_for_image(image);
  raw_clip_sample_count_ = raw_clipping.sample_count;
  raw_shadow_clip_count_ = raw_clipping.shadow_clip_count;
  raw_highlight_clip_count_ = raw_clipping.highlight_clip_count;
  emit histogramChanged();
}

QSize EditorPreviewController::activeBrushLivePreviewSize() const {
  const auto source_size = sourceImageSize();
  if (!source_size.isValid()) {
    return {};
  }
  const auto preview_edge = qBound(
      kDraftInteractivePreviewEdge,
      std::max(
          interactive_preview_edge_for_state(currentState(), previewTargetEdge()),
          kExpensiveInteractivePreviewEdge),
      kMaximumInteractivePreviewEdge);
  return scaled_size_for_max_edge(source_size, preview_edge);
}

void EditorPreviewController::updateActiveBrushLivePreview(
    const QVector<BrushPreviewPoint>& sampled_points) {
  if (sampled_points.isEmpty() || brush_preview_compositor_ == nullptr ||
      !brush_preview_compositor_->available()) {
    return;
  }

  const auto preview_size = activeBrushLivePreviewSize();
  if (!preview_size.isValid() ||
      !brush_preview_compositor_->ensureTarget(&active_brush_live_preview_image_, preview_size)) {
    return;
  }

  const auto mask = activeMaskDefinition();
  brush_preview_compositor_->compositePoints(
      &active_brush_live_preview_image_,
      sampled_points,
      BrushPreviewStyle{
          .color = brush_preview_color_for_mask(mask, maskPreviewMode()),
          .feather = activeMaskFeather(),
          .hardness = activeMaskBrushHardness(),
          .opacity = brush_preview_opacity_for_mask(mask, maskPreviewMode()),
      });

  if (image_provider_ != nullptr &&
      !brush_preview_compositor_->hasNativeTexture() &&
      !active_brush_live_preview_image_.isNull()) {
    image_provider_->setImage(
        QString::fromLatin1(kActiveBrushLivePreviewImageId),
        active_brush_live_preview_image_);
  }
  ++active_brush_live_preview_revision_;
}

void EditorPreviewController::clearActiveBrushLivePreview() {
  active_brush_live_preview_image_ = QImage{};
  if (image_provider_ != nullptr) {
    image_provider_->clearImage(QString::fromLatin1(kActiveBrushLivePreviewImageId));
  }
  if (brush_preview_compositor_ != nullptr) {
    brush_preview_compositor_->clear(&active_brush_live_preview_image_);
  }
  ++active_brush_live_preview_revision_;
}

void EditorPreviewController::resetSmartSelectPreview() {
  ++smart_select_preview_generation_;
  smart_select_preview_timer_.stop();
  smart_select_preview_request_pending_ = false;
  clearSmartSelectPreviewState();
}

void EditorPreviewController::startSmartSelectPreviewRequest() {
  if (!smart_select_preview_request_pending_ || smart_select_preview_watcher_.isRunning()) {
    return;
  }

  const auto request = pending_smart_select_preview_request_;
  smart_select_preview_request_pending_ = false;
  if (!vision_segmentation_available()) {
    const auto result = compute_smart_select_preview_result(request);
    if (result.success && result.generation == smart_select_preview_generation_) {
      updateSmartSelectPreviewState(result.preview_mask, result.instance_ids);
    } else if (!result.success &&
               result.generation == smart_select_preview_generation_ &&
               !smart_select_preview_request_pending_) {
      resetSmartSelectPreview();
    }
    if (smart_select_preview_request_pending_ && !smart_select_preview_timer_.isActive()) {
      startSmartSelectPreviewRequest();
    }
    return;
  }
  smart_select_preview_watcher_.setFuture(
      QtConcurrent::run([request]() { return compute_smart_select_preview_result(request); }));
}

void EditorPreviewController::handleSmartSelectPreviewFinished() {
  const auto result = smart_select_preview_watcher_.result();
  if (result.success && result.generation == smart_select_preview_generation_) {
    updateSmartSelectPreviewState(result.preview_mask, result.instance_ids);
  } else if (!result.success &&
             result.generation == smart_select_preview_generation_ &&
             !smart_select_preview_request_pending_) {
    resetSmartSelectPreview();
  }

  if (smart_select_preview_request_pending_ && !smart_select_preview_timer_.isActive()) {
    startSmartSelectPreviewRequest();
  }
}

void EditorPreviewController::updateSmartSelectPreviewState(
    const QVariantMap& preview_mask,
    const QVector<int>& instance_ids,
    const QVector<int>& candidate_instance_ids) {
  const auto normalized_candidate_index = candidate_instance_ids.isEmpty()
                                              ? 0
                                              : (candidate_instance_ids ==
                                                         smart_select_preview_candidate_instance_ids_
                                                     ? qBound(
                                                           0,
                                                           smart_select_preview_candidate_index_,
                                                           static_cast<int>(candidate_instance_ids.size()) - 1)
                                                     : 0);
  if (smart_select_preview_instance_ids_ == instance_ids &&
      smart_select_preview_candidate_instance_ids_ == candidate_instance_ids &&
      smart_select_preview_candidate_index_ == normalized_candidate_index &&
      smart_select_preview_mask_ == preview_mask) {
    return;
  }
  smart_select_preview_instance_ids_ = instance_ids;
  smart_select_preview_candidate_instance_ids_ = candidate_instance_ids;
  smart_select_preview_candidate_index_ = normalized_candidate_index;
  smart_select_preview_mask_ = preview_mask;
  emit smartSelectPreviewChanged();
}

void EditorPreviewController::updateSmartSelectPreviewResult(
    const SegmentationResult& result,
    const QVector<int>& instance_ids) {
  if (!result.success) {
    clearSmartSelectPreviewState();
    return;
  }

  const auto preview_mask =
      segmentation_mask_variant(result, QStringLiteral("object"));
  if (preview_mask.isEmpty()) {
    clearSmartSelectPreviewState();
    return;
  }

  updateSmartSelectPreviewState(preview_mask, instance_ids);
}

void EditorPreviewController::clearSmartSelectPreviewState() {
  updateSmartSelectPreviewState({}, {});
}

void EditorPreviewController::resetSmartSelectInstanceMap() {
  smart_select_instance_map_key_.clear();
  smart_select_instance_map_data_.clear();
  smart_select_instance_map_width_ = 0;
  smart_select_instance_map_height_ = 0;
  resetSmartSelectPreview();
}

bool EditorPreviewController::ensureSmartSelectInstanceMap() {
  if (!vision_segmentation_available() || file_path_.isEmpty()) {
    resetSmartSelectInstanceMap();
    return false;
  }

  const auto source_info = QFileInfo(file_path_);
  auto source_key = decoded_image_cache_key(source_info, 1024);
  if (raw_image_supported(file_path_)) {
    source_key += QStringLiteral("|raw|") + raw_decode_settings_key(currentState());
  }
  if (source_key == smart_select_instance_map_key_ &&
      !smart_select_instance_map_data_.isEmpty() &&
      smart_select_instance_map_width_ > 0 &&
      smart_select_instance_map_height_ > 0) {
    return true;
  }

  QImage source_image;
  if (!decodeSourceImage(&source_image, 1024)) {
    resetSmartSelectInstanceMap();
    return false;
  }

  const auto instance_map = generate_foreground_instance_map(source_image);
  if (!instance_map.success) {
    resetSmartSelectInstanceMap();
    return false;
  }

  smart_select_instance_map_key_ = source_key;
  smart_select_instance_map_data_ = instance_map.instance_data;
  smart_select_instance_map_width_ = instance_map.width;
  smart_select_instance_map_height_ = instance_map.height;
  return true;
}

int EditorPreviewController::smartSelectInstanceIdAt(
    double normalized_x,
    double normalized_y) const {
  const auto instance_ids =
      smartSelectCandidateInstanceIdsAt(normalized_x, normalized_y);
  return instance_ids.isEmpty() ? 0 : instance_ids.first();
}

QVector<int> EditorPreviewController::smartSelectCandidateInstanceIdsAt(
    double normalized_x,
    double normalized_y) const {
  return smart_select_instance_candidates_from_map(
      smart_select_instance_map_data_,
      smart_select_instance_map_width_,
      smart_select_instance_map_height_,
      qBound(0.0, normalized_x, 1.0),
      qBound(0.0, normalized_y, 1.0));
}

QVector<int> EditorPreviewController::smartSelectCandidateInstanceIdsForPath(
    const QVariantList& points) const {
  return smart_select_instance_candidates_for_path(
      smart_select_instance_map_data_,
      smart_select_instance_map_width_,
      smart_select_instance_map_height_,
      normalized_smart_select_path(points));
}

QVariantMap EditorPreviewController::smartSelectPreviewMaskForInstances(
    const QVector<int>& instance_ids) const {
  return segmentation_mask_for_instances(
      smart_select_instance_map_data_,
      smart_select_instance_map_width_,
      smart_select_instance_map_height_,
      instance_ids);
}

int EditorPreviewController::findPendingRetouchMaskIndex(
    const QString& retouch_mode,
    const QString& kind) const {
  const auto normalized_retouch_mode = retouch_mode.trimmed().toLower();
  const auto normalized_kind =
      kind.trimmed().isEmpty() ? QString() : normalized_mask_kind(kind);
  for (int index = 0; index < mask_stack_.size(); ++index) {
    const auto mask = mask_stack_.at(index).toMap();
    if (mask.value(QStringLiteral("toolRole")).toString() != QStringLiteral("retouch") ||
        mask.value(QStringLiteral("committed")).toBool() ||
        mask.value(QStringLiteral("retouchMode")).toString().trimmed().toLower() !=
            normalized_retouch_mode) {
      continue;
    }
    if (!normalized_kind.isEmpty() &&
        mask.value(QStringLiteral("kind")).toString() != normalized_kind) {
      continue;
    }
    return index;
  }
  return -1;
}

int EditorPreviewController::findReusableRetouchBrushIndex(const QString& retouch_mode) const {
  const auto normalized_retouch_mode = retouch_mode.trimmed().toLower();
  if (hasActiveMask()) {
    const auto active_mask = activeMaskDefinition();
    if (active_mask.value(QStringLiteral("kind")).toString() == QStringLiteral("brush") &&
        active_mask.value(QStringLiteral("toolRole")).toString() == QStringLiteral("retouch") &&
        !active_mask.value(QStringLiteral("committed")).toBool() &&
        active_mask.value(QStringLiteral("retouchMode")).toString().trimmed().toLower() ==
            normalized_retouch_mode) {
      return active_mask_index_;
    }
  }

  return findPendingRetouchMaskIndex(normalized_retouch_mode, QStringLiteral("brush"));
}

QVariantMap EditorPreviewController::buildBrushMask(
    const QString& name,
    const QString& operation,
    const QString& tool_role,
    const QString& retouch_mode,
    bool committed,
    int ordinal) const {
  auto mask = default_mask_definition(QStringLiteral("brush"), ordinal);
  mask.insert(QStringLiteral("name"), name);
  mask.insert(QStringLiteral("operation"), normalized_mask_operation(operation));
  mask.insert(QStringLiteral("toolRole"), normalized_mask_tool_role(tool_role));
  mask.insert(QStringLiteral("retouchMode"), retouch_mode.trimmed().toLower());
  mask.insert(QStringLiteral("committed"), committed);
  return mask;
}

bool EditorPreviewController::channelMatchesSelection(const QColor& color) const {
  return channel_weight_for_selection(color.hslHueF(), hsl_channel_) > 0.0;
}

QString EditorPreviewController::saveRecipeProgram() const {
  return AppBootstrap::helperProgram(QStringLiteral("save_edit_recipe"));
}

QStringList EditorPreviewController::saveRecipeArguments(const QString& asset_id, const QString& recipe_json) const {
  return AppBootstrap::helperArguments(QStringLiteral("save_edit_recipe"), {asset_id, recipe_json});
}

HistogramComputationResult compute_histogram_data(const QImage& image) {
  constexpr int kBins = 256;
  constexpr int kScopeColumns = 128;
  constexpr int kScopeLevels = 64;
  constexpr int kVectorscopeSampleTarget = 1800;
  HistogramComputationResult result;
  std::array<int, kBins> red_bins{};
  std::array<int, kBins> green_bins{};
  std::array<int, kBins> blue_bins{};
  std::array<int, kBins> luma_bins{};
  std::array<int, kScopeColumns * kScopeLevels> red_scope_density{};
  std::array<int, kScopeColumns * kScopeLevels> green_scope_density{};
  std::array<int, kScopeColumns * kScopeLevels> blue_scope_density{};
  std::array<int, kScopeColumns * kScopeLevels> luma_scope_density{};
  std::array<double, kScopeColumns> red_scope_sum{};
  std::array<double, kScopeColumns> green_scope_sum{};
  std::array<double, kScopeColumns> blue_scope_sum{};
  std::array<double, kScopeColumns> luma_scope_sum{};
  std::array<double, kScopeColumns> red_scope_min{};
  std::array<double, kScopeColumns> green_scope_min{};
  std::array<double, kScopeColumns> blue_scope_min{};
  std::array<double, kScopeColumns> luma_scope_min{};
  std::array<double, kScopeColumns> red_scope_max{};
  std::array<double, kScopeColumns> green_scope_max{};
  std::array<double, kScopeColumns> blue_scope_max{};
  std::array<double, kScopeColumns> luma_scope_max{};
  std::array<int, kScopeColumns> scope_counts{};
  QVariantList vectorscope_points;

  red_scope_min.fill(1.0);
  green_scope_min.fill(1.0);
  blue_scope_min.fill(1.0);
  luma_scope_min.fill(1.0);

  const auto w = image.width();
  const auto h = image.height();
  result.sample_count = std::max(0, w * h);
  result.shadow_clip_count = 0;
  result.highlight_clip_count = 0;
  const auto sample_stride = std::max(1, static_cast<int>(std::sqrt(
                                      static_cast<double>(std::max(1, w * h)) /
                                      static_cast<double>(kVectorscopeSampleTarget))));
  auto density_slot = [](int column, double value) {
    const auto level = std::clamp(
        static_cast<int>(value * static_cast<double>(kScopeLevels - 1) + 0.5),
        0,
        kScopeLevels - 1);
    return static_cast<std::size_t>(column * kScopeLevels + level);
  };
  for (int y = 0; y < h; ++y) {
    const auto* scanline = reinterpret_cast<const QRgb*>(image.constScanLine(y));
    for (int x = 0; x < w; ++x) {
      const auto pixel = scanline[x];
      const auto r = qRed(pixel);
      const auto g = qGreen(pixel);
      const auto b = qBlue(pixel);
      const auto luma = static_cast<int>(0.2126 * r + 0.7152 * g + 0.0722 * b + 0.5);
      if (r == 0 || g == 0 || b == 0) {
        ++result.shadow_clip_count;
      }
      if (r == 255 || g == 255 || b == 255) {
        ++result.highlight_clip_count;
      }
      ++red_bins[static_cast<std::size_t>(r)];
      ++green_bins[static_cast<std::size_t>(g)];
      ++blue_bins[static_cast<std::size_t>(b)];
      ++luma_bins[static_cast<std::size_t>(std::clamp(luma, 0, 255))];

      const auto scope_index = std::clamp(
          static_cast<int>((static_cast<double>(x) / static_cast<double>(std::max(1, w))) *
                           static_cast<double>(kScopeColumns)),
          0,
          kScopeColumns - 1);
      const auto scope_slot = static_cast<std::size_t>(scope_index);
      const auto rn = r / 255.0;
      const auto gn = g / 255.0;
      const auto bn = b / 255.0;
      const auto ln = std::clamp(luma, 0, 255) / 255.0;
      ++red_scope_density[density_slot(scope_index, rn)];
      ++green_scope_density[density_slot(scope_index, gn)];
      ++blue_scope_density[density_slot(scope_index, bn)];
      ++luma_scope_density[density_slot(scope_index, ln)];
      red_scope_sum[scope_slot] += rn;
      green_scope_sum[scope_slot] += gn;
      blue_scope_sum[scope_slot] += bn;
      luma_scope_sum[scope_slot] += ln;
      red_scope_min[scope_slot] = std::min(red_scope_min[scope_slot], rn);
      green_scope_min[scope_slot] = std::min(green_scope_min[scope_slot], gn);
      blue_scope_min[scope_slot] = std::min(blue_scope_min[scope_slot], bn);
      luma_scope_min[scope_slot] = std::min(luma_scope_min[scope_slot], ln);
      red_scope_max[scope_slot] = std::max(red_scope_max[scope_slot], rn);
      green_scope_max[scope_slot] = std::max(green_scope_max[scope_slot], gn);
      blue_scope_max[scope_slot] = std::max(blue_scope_max[scope_slot], bn);
      luma_scope_max[scope_slot] = std::max(luma_scope_max[scope_slot], ln);
      ++scope_counts[scope_slot];

      if ((x % sample_stride) == 0 && (y % sample_stride) == 0 &&
          vectorscope_points.size() < kVectorscopeSampleTarget) {
        const auto y_value = 0.2126 * rn + 0.7152 * gn + 0.0722 * bn;
        const auto cb = (bn - y_value) * 0.565;
        const auto cr = (rn - y_value) * 0.713;
        QVariantMap point;
        point.insert(QStringLiteral("x"), std::clamp(0.5 + cb, 0.0, 1.0));
        point.insert(QStringLiteral("y"), std::clamp(0.5 - cr, 0.0, 1.0));
        point.insert(QStringLiteral("luma"), std::clamp(y_value, 0.0, 1.0));
        point.insert(QStringLiteral("red"), rn);
        point.insert(QStringLiteral("green"), gn);
        point.insert(QStringLiteral("blue"), bn);
        vectorscope_points.append(point);
      }
    }
  }

  // Normalize to [0, 1] range for display
  const auto max_count = std::max({
      *std::max_element(red_bins.begin(), red_bins.end()),
      *std::max_element(green_bins.begin(), green_bins.end()),
      *std::max_element(blue_bins.begin(), blue_bins.end()),
      *std::max_element(luma_bins.begin(), luma_bins.end()),
  });

  if (max_count <= 0) {
    return result;
  }

  const auto scale = 1.0 / static_cast<double>(max_count);
  QVariantList r_list, g_list, b_list, l_list;
  QVariantList scope_r_list, scope_g_list, scope_b_list, scope_l_list;
  QVariantList density_r_list, density_g_list, density_b_list, density_l_list;
  r_list.reserve(kBins);
  g_list.reserve(kBins);
  b_list.reserve(kBins);
  l_list.reserve(kBins);
  scope_r_list.reserve(kScopeColumns);
  scope_g_list.reserve(kScopeColumns);
  scope_b_list.reserve(kScopeColumns);
  scope_l_list.reserve(kScopeColumns);
  density_r_list.reserve(kScopeColumns * kScopeLevels);
  density_g_list.reserve(kScopeColumns * kScopeLevels);
  density_b_list.reserve(kScopeColumns * kScopeLevels);
  density_l_list.reserve(kScopeColumns * kScopeLevels);
  for (int i = 0; i < kBins; ++i) {
    r_list.append(static_cast<double>(red_bins[static_cast<std::size_t>(i)]) * scale);
    g_list.append(static_cast<double>(green_bins[static_cast<std::size_t>(i)]) * scale);
    b_list.append(static_cast<double>(blue_bins[static_cast<std::size_t>(i)]) * scale);
    l_list.append(static_cast<double>(luma_bins[static_cast<std::size_t>(i)]) * scale);
  }
  for (int i = 0; i < kScopeColumns; ++i) {
    const auto slot = static_cast<std::size_t>(i);
    const auto count = scope_counts[slot];
    auto scope_sample = [count](double min_value, double max_value, double sum_value) {
      QVariantMap sample;
      if (count <= 0) {
        sample.insert(QStringLiteral("min"), 0.0);
        sample.insert(QStringLiteral("max"), 0.0);
        sample.insert(QStringLiteral("avg"), 0.0);
        return sample;
      }
      sample.insert(QStringLiteral("min"), std::clamp(min_value, 0.0, 1.0));
      sample.insert(QStringLiteral("max"), std::clamp(max_value, 0.0, 1.0));
      sample.insert(QStringLiteral("avg"), std::clamp(sum_value / static_cast<double>(count), 0.0, 1.0));
      return sample;
    };
    scope_r_list.append(scope_sample(red_scope_min[slot], red_scope_max[slot], red_scope_sum[slot]));
    scope_g_list.append(scope_sample(green_scope_min[slot], green_scope_max[slot], green_scope_sum[slot]));
    scope_b_list.append(scope_sample(blue_scope_min[slot], blue_scope_max[slot], blue_scope_sum[slot]));
    scope_l_list.append(scope_sample(luma_scope_min[slot], luma_scope_max[slot], luma_scope_sum[slot]));
  }
  const auto max_density = std::max({
      *std::max_element(red_scope_density.begin(), red_scope_density.end()),
      *std::max_element(green_scope_density.begin(), green_scope_density.end()),
      *std::max_element(blue_scope_density.begin(), blue_scope_density.end()),
      *std::max_element(luma_scope_density.begin(), luma_scope_density.end()),
      1,
  });
  const auto density_scale = 1.0 / std::log1p(static_cast<double>(max_density));
  auto append_density = [density_scale](const auto& source, QVariantList& target) {
    for (const auto count : source) {
      target.append(std::log1p(static_cast<double>(count)) * density_scale);
    }
  };
  append_density(red_scope_density, density_r_list);
  append_density(green_scope_density, density_g_list);
  append_density(blue_scope_density, density_b_list);
  append_density(luma_scope_density, density_l_list);

  result.red = std::move(r_list);
  result.green = std::move(g_list);
  result.blue = std::move(b_list);
  result.luminance = std::move(l_list);
  result.waveform_red = std::move(scope_r_list);
  result.waveform_green = std::move(scope_g_list);
  result.waveform_blue = std::move(scope_b_list);
  result.waveform_luminance = std::move(scope_l_list);
  result.density_red = std::move(density_r_list);
  result.density_green = std::move(density_g_list);
  result.density_blue = std::move(density_b_list);
  result.density_luminance = std::move(density_l_list);
  result.vectorscope_points = std::move(vectorscope_points);
  return result;
}

void EditorPreviewController::computeHistogram(const QImage& image) {
  applyHistogramResult(compute_histogram_data(image));
}

void EditorPreviewController::applyHistogramResult(const HistogramComputationResult& result) {
  histogram_red_ = result.red;
  histogram_green_ = result.green;
  histogram_blue_ = result.blue;
  histogram_luminance_ = result.luminance;
  scope_waveform_red_ = result.waveform_red;
  scope_waveform_green_ = result.waveform_green;
  scope_waveform_blue_ = result.waveform_blue;
  scope_waveform_luminance_ = result.waveform_luminance;
  scope_density_red_ = result.density_red;
  scope_density_green_ = result.density_green;
  scope_density_blue_ = result.density_blue;
  scope_density_luminance_ = result.density_luminance;
  scope_vectorscope_points_ = result.vectorscope_points;
  histogram_shadow_clip_count_ = result.shadow_clip_count;
  histogram_highlight_clip_count_ = result.highlight_clip_count;
  histogram_sample_count_ = result.sample_count;
  raw_clip_sample_count_ = result.raw_clip_sample_count;
  raw_shadow_clip_count_ = result.raw_shadow_clip_count;
  raw_highlight_clip_count_ = result.raw_highlight_clip_count;
  emit histogramChanged();
}

void EditorPreviewController::setHistogramUpdateActive(bool active) {
  if (histogram_update_active_ == active) {
    return;
  }
  histogram_update_active_ = active;
  emit histogramActivityChanged();
}

void EditorPreviewController::startHistogramUpdate(const RenderResult& result) {
  startHistogramUpdate(
      result.histogram_image,
      result.generation,
      result.full_resolution,
      result.raw_clip_sample_count,
      result.raw_shadow_clip_count,
      result.raw_highlight_clip_count,
      result.render_reason,
      result.interaction_mode);
}

void EditorPreviewController::startHistogramUpdate(
    const QImage& image,
    int generation,
    bool full_resolution,
    int raw_clip_sample_count,
    int raw_shadow_clip_count,
    int raw_highlight_clip_count,
    const QString& render_reason,
    const QString& interaction_mode) {
  if (image.isNull()) {
    return;
  }

  if (histogram_watcher_.isRunning()) {
    pending_histogram_image_ = image;
    pending_histogram_generation_ = generation;
    pending_histogram_full_resolution_ = full_resolution;
    pending_histogram_raw_clip_sample_count_ = raw_clip_sample_count;
    pending_histogram_raw_shadow_clip_count_ = raw_shadow_clip_count;
    pending_histogram_raw_highlight_clip_count_ = raw_highlight_clip_count;
    pending_histogram_render_reason_ = render_reason;
    pending_histogram_interaction_mode_ = interaction_mode;
    histogram_update_pending_ = true;
    setHistogramUpdateActive(true);
    updateProcessingState();
    return;
  }

  setHistogramUpdateActive(true);
  histogram_watcher_.setFuture(QtConcurrent::run(
      [image,
       generation,
       full_resolution,
       raw_clip_sample_count,
       raw_shadow_clip_count,
       raw_highlight_clip_count,
       render_reason,
       interaction_mode]() {
        QElapsedTimer timer;
        timer.start();
        auto result = compute_histogram_data(image);
        result.generation = generation;
        result.full_resolution = full_resolution;
        result.raw_clip_sample_count = raw_clip_sample_count;
        result.raw_shadow_clip_count = raw_shadow_clip_count;
        result.raw_highlight_clip_count = raw_highlight_clip_count;
        result.render_reason = render_reason;
        result.interaction_mode = interaction_mode;
        result.elapsed_ms = timer.elapsed();
        return result;
      }));
  updateProcessingState();
}

void EditorPreviewController::handleHistogramFinished() {
  const auto result = histogram_watcher_.result();
  if (result.generation == render_generation_) {
    applyHistogramResult(result);
    observability::logInfo(
        photoEditorPerfLog(),
        QStringLiteral("preview.histogram.completed"),
        {
            {QStringLiteral("stage"), result.full_resolution ? QStringLiteral("full") : QStringLiteral("interactive")},
            {QStringLiteral("generation"), result.generation},
            {QStringLiteral("render_reason"), result.render_reason},
            {QStringLiteral("interaction_mode"), result.interaction_mode},
            {QStringLiteral("sample_count"), result.sample_count},
            {QStringLiteral("shadow_clip_count"), result.shadow_clip_count},
            {QStringLiteral("highlight_clip_count"), result.highlight_clip_count},
            {QStringLiteral("elapsed_ms"), result.elapsed_ms},
        });
  }

  if (histogram_update_pending_) {
    const auto image = pending_histogram_image_;
    const auto generation = pending_histogram_generation_;
    const auto full_resolution = pending_histogram_full_resolution_;
    const auto raw_clip_sample_count = pending_histogram_raw_clip_sample_count_;
    const auto raw_shadow_clip_count = pending_histogram_raw_shadow_clip_count_;
    const auto raw_highlight_clip_count = pending_histogram_raw_highlight_clip_count_;
    const auto render_reason = pending_histogram_render_reason_;
    const auto interaction_mode = pending_histogram_interaction_mode_;
    pending_histogram_image_ = QImage();
    pending_histogram_generation_ = 0;
    pending_histogram_full_resolution_ = false;
    pending_histogram_raw_clip_sample_count_ = 0;
    pending_histogram_raw_shadow_clip_count_ = 0;
    pending_histogram_raw_highlight_clip_count_ = 0;
    pending_histogram_render_reason_.clear();
    pending_histogram_interaction_mode_.clear();
    histogram_update_pending_ = false;
    startHistogramUpdate(
        image,
        generation,
        full_resolution,
        raw_clip_sample_count,
        raw_shadow_clip_count,
        raw_highlight_clip_count,
        render_reason,
        interaction_mode);
    return;
  }

  setHistogramUpdateActive(false);
  updateProcessingState();
}

}  // namespace photo_editor::desktop

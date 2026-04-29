#include "editor_preview_controller.h"

#include "ai_model_runtime.h"
#include "app_bootstrap.h"
#include "brush_preview_compositor.h"
#include "editor_preview_image_sink.h"
#include "editor_preview_texture.h"
#include "observability.h"
#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"
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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <limits>
#include <queue>
#include <vector>

namespace photo_editor::desktop {

namespace {

constexpr int kDefaultInteractivePreviewEdge = 960;
constexpr int kMinimumInteractivePreviewEdge = 512;
constexpr int kMaximumInteractivePreviewEdge = 4096;
constexpr int kExpensiveInteractivePreviewEdge = 2048;
constexpr int kDraftInteractivePreviewEdge = 1024;
constexpr int kExpensiveDraftInteractivePreviewEdge = 512;
constexpr int kHistogramPreviewEdge = 1024;
constexpr int kFullRenderDebounceMs = 700;
constexpr int kExpensiveInteractiveRenderDebounceMs = 45;
constexpr int kExpensiveFullRenderDebounceMs = 900;
constexpr int kSmartSelectPreviewDebounceMs = 45;
constexpr qint64 kInteractiveDraftBurstWindowMs = 180;
constexpr qint64 kMaxCacheMemoryBytes = 512 * 1024 * 1024;  // 512 MB
constexpr qint64 kMaxLinearCacheMemoryBytes = 256 * 1024 * 1024;  // 256 MB
constexpr int kMaxEditHistoryDepth = 100;
constexpr auto kEditedPreviewImageId = "edited";
constexpr auto kReferencePreviewImageId = "reference";
constexpr auto kEditedPreviewOverlayImageId = "edited-overlay";
constexpr auto kReferencePreviewOverlayImageId = "reference-overlay";
constexpr auto kActiveBrushLivePreviewImageId = "brush-live";

struct DecodedImageCache final {
  QMutex mutex;
  QHash<QString, QImage> images;
  QList<QString> access_order;  // front = most recently used
  qint64 total_bytes = 0;

  void touch(const QString& key) {
    access_order.removeOne(key);
    access_order.prepend(key);
  }

  void insert(const QString& key, const QImage& image) {
    if (images.contains(key)) {
      total_bytes -= images.value(key).sizeInBytes();
      images.remove(key);
      access_order.removeOne(key);
    }
    const auto image_bytes = image.sizeInBytes();
    while (total_bytes + image_bytes > kMaxCacheMemoryBytes && !access_order.isEmpty()) {
      const auto& evict_key = access_order.last();
      total_bytes -= images.value(evict_key).sizeInBytes();
      images.remove(evict_key);
      access_order.removeLast();
    }
    images.insert(key, image);
    access_order.prepend(key);
    total_bytes += image_bytes;
  }
};

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

DecodedImageCache& decoded_image_cache() {
  static DecodedImageCache cache;
  return cache;
}

LinearImageCache& linear_image_cache() {
  static LinearImageCache cache;
  return cache;
}

QString decoded_image_cache_key(const QFileInfo& file_info, int max_edge) {
  return QStringLiteral("%1|%2|%3|%4")
      .arg(file_info.absoluteFilePath())
      .arg(max_edge)
      .arg(file_info.size())
      .arg(file_info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch());
}

RawDecodeSettings raw_decode_settings_for_state(const EditState& state) {
  return RawDecodeSettings{
      .input_transform_enabled = state.raw_input_transform_enabled,
      .input_color_profile = state.input_color_profile,
      .highlight_reconstruction_mode = state.highlight_reconstruction_mode,
      .chromatic_adaptation_method = state.chromatic_adaptation_method,
      .exposure = state.raw_exposure,
      .boost_amount = state.raw_boost,
      .boost_shadow_amount = state.raw_shadow_boost,
      .detail_amount = state.raw_detail,
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
  };
}

photo_editor::pipeline::ColorPipelineConfig color_pipeline_config_for_state(
    const EditState& state,
    const photo_editor::pipeline::DecodedImageMetadata* metadata) {
  auto config = color_pipeline_config_for_state(state);
  if (metadata != nullptr && metadata->valid) {
    config.camera_profile = photo_editor::pipeline::resolve_camera_profile(*metadata);
    config.input_transform_recipe = photo_editor::pipeline::resolve_input_transform_recipe(
        config.input_profile,
        config.camera_profile,
        *metadata);
  }
  return config;
}

QString raw_decode_settings_key(const EditState& state) {
  return QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11|%12|%13|%14")
      .arg(state.raw_input_transform_enabled ? 1 : 0)
      .arg(normalized_input_color_profile_name(state.input_color_profile))
      .arg(normalized_highlight_reconstruction_mode_name(state.highlight_reconstruction_mode))
      .arg(normalized_chromatic_adaptation_method_name(state.chromatic_adaptation_method))
      .arg(state.raw_exposure, 0, 'f', 6)
      .arg(state.raw_boost, 0, 'f', 6)
      .arg(state.raw_shadow_boost, 0, 'f', 6)
      .arg(state.raw_detail, 0, 'f', 6)
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
    sources.append(source);
    set.insert(QStringLiteral("sources"), sources);
    set.insert(QStringLiteral("sourceCount"), sources.size());
    grouped[set_index] = set;
  }
  return grouped;
}

QVariantList grouped_retouch_stack(const QVariantList& masks) {
  QVariantList grouped;
  auto ordinal = 1;
  for (const auto& value : masks) {
    auto mask = normalized_mask_definition(value.toMap(), ordinal);
    if (is_retouch_mask(mask)) {
      mask.insert(QStringLiteral("flatIndex"), ordinal - 1);
      grouped.append(mask);
    }
    ++ordinal;
  }
  return grouped;
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
    double brush_flow) {
  QVariantMap point;
  point.insert(QStringLiteral("x"), normalized_x);
  point.insert(QStringLiteral("y"), normalized_y);
  point.insert(QStringLiteral("size"), std::max(0.01, brush_size));
  point.insert(QStringLiteral("flow"), clamp_unit(brush_flow));
  return point;
}

QVariantMap brush_point_variant(const BrushPreviewPoint& point) {
  return brush_point_variant(point.x, point.y, point.size, point.flow);
}

BrushPreviewPoint brush_preview_point(
    double normalized_x,
    double normalized_y,
    double brush_size,
    double brush_flow) {
  return BrushPreviewPoint{
      .x = normalized_x,
      .y = normalized_y,
      .size = std::max(0.01, brush_size),
      .flow = clamp_unit(brush_flow),
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
  };
}

QVector<BrushPreviewPoint> sampled_brush_points_for_segment(
    const BrushPreviewPoint* last_point,
    double normalized_x,
    double normalized_y,
    double brush_size,
    double brush_flow) {
  QVector<BrushPreviewPoint> sampled_points;
  const auto clamped_point =
      brush_preview_point(normalized_x, normalized_y, brush_size, brush_flow);

  if (last_point == nullptr) {
    sampled_points.append(clamped_point);
    return sampled_points;
  }

  const auto distance = std::hypot(clamped_point.x - last_point->x, clamped_point.y - last_point->y);
  const auto spacing = std::clamp(clamped_point.size * 0.75, 0.0025, 0.05);
  if (distance <= spacing * 0.35) {
    return sampled_points;
  }

  const auto steps = std::max(1, static_cast<int>(std::ceil(distance / spacing)));
  sampled_points.reserve(steps);
  for (int step = 1; step <= steps; ++step) {
    const auto t = static_cast<double>(step) / static_cast<double>(steps);
    sampled_points.append(brush_preview_point(
        std::lerp(last_point->x, clamped_point.x, t),
        std::lerp(last_point->y, clamped_point.y, t),
        clamped_point.size,
        clamped_point.flow));
  }
  return sampled_points;
}

void rasterize_brush_point_into_mask(
    QVariantMap* mask,
    double normalized_x,
    double normalized_y,
    double brush_size,
    double brush_flow,
    double feather) {
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
      if (weight <= 1e-6) {
        continue;
      }
      const auto index = y * width + x;
      const auto existing =
          static_cast<unsigned char>(data.at(index));
      data[index] = static_cast<char>(std::max(
          static_cast<int>(existing),
          static_cast<int>(std::lround(clamp_unit(weight) * 255.0))));
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
  if (kind == QStringLiteral("polygon")) {
    QVariantList outline_paths;
    outline_paths.append(mask.value(QStringLiteral("polygonPoints")).toList());
    return outline_paths;
  }
  const auto segmentation_data = mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto segmentation_width = mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto segmentation_height = mask.value(QStringLiteral("segmentationHeight")).toInt();
  if ((kind == QStringLiteral("subject") || kind == QStringLiteral("object") ||
       kind == QStringLiteral("brush")) &&
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

double apply_selection_refine_to_preview_weight(
    double base_weight,
    const photo_editor::pipeline::LocalMask& mask) {
  auto refined = apply_selection_expand_to_preview_weight(base_weight, mask);
  refined = apply_selection_edge_contrast_to_preview_weight(refined, mask);
  if (mask.selection_invert) {
    refined = 1.0 - refined;
  }
  return clamp_unit(refined);
}

enum class MaskPreviewMode {
  Overlay,
  Grayscale,
  BlackOnWhite,
  WhiteOnBlack,
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
  local_mask.selection_edge_contrast =
      map_double_value(mask, QStringLiteral("selectionEdgeContrast"), 0.0);
  local_mask.selection_decontaminate =
      map_double_value(mask, QStringLiteral("selectionDecontaminate"), 0.0);
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
  } else if (local_mask.kind == "polygon") {
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
  } else if (local_mask.kind == "subject" || local_mask.kind == "object") {
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
    return apply_selection_refine_to_preview_weight(
        clamp_unit(lower_weight * upper_weight),
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
    return apply_selection_refine_to_preview_weight(
        clamp_unit(hue_weight * sat_lower * sat_upper),
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
      (mask.kind != "luminance" && mask.kind != "color")) {
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
      const auto weight = range_mask_weight_for_preview_pixel(linear_pixel, mask);
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
  const auto softness = std::clamp(mask.feather, 0.05, 1.0);
  const auto inner_scale = std::max(0.0, 1.0 - softness);
  for (const auto& point : mask.brush_points) {
    const auto brush_size = std::max(0.01, point.size);
    const auto dx = normalized_x - point.x;
    const auto dy = normalized_y - point.y;
    const auto distance = std::sqrt(dx * dx + dy * dy) / brush_size;
    const auto point_weight =
        clamp_unit(point.flow) * (1.0 - smoothstep(inner_scale, 1.0, distance));
    weight = 1.0 - (1.0 - weight) * (1.0 - point_weight);
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

  if (mask.kind == "subject" || mask.kind == "object") {
    return apply_selection_refine_to_preview_weight(
        subject_object_mask_weight_for_preview(normalized_x, normalized_y, mask),
        mask);
  }
  if (mask.kind == "radial") {
    return apply_selection_refine_to_preview_weight(
        radial_mask_weight_for_preview(normalized_x, normalized_y, mask),
        mask);
  }
  if (mask.kind == "brush") {
    return apply_selection_refine_to_preview_weight(
        brush_mask_weight_for_preview(normalized_x, normalized_y, mask),
        mask);
  }
  if (mask.kind == "polygon") {
    return apply_selection_refine_to_preview_weight(
        polygon_mask_weight_for_preview(normalized_x, normalized_y, mask),
        mask);
  }
  if (mask.kind == "luminance" || mask.kind == "color") {
    return range_mask_weight_for_preview_pixel(pixel, mask);
  }
  return apply_selection_refine_to_preview_weight(
      linear_mask_weight_for_preview(normalized_x, normalized_y, mask),
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
  auto ordinal = 1;
  for (const auto& value : masks) {
    const auto mask = normalized_mask_definition(value.toMap(), ordinal);
    ++ordinal;
    if (!mask_definition_affects_render(mask, ordinal - 1) ||
        pipeline_mask_tool_role(mask) != QStringLiteral("selection")) {
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
  auto ordinal = 1;
  for (const auto& value : masks) {
    const auto mask = normalized_mask_definition(value.toMap(), ordinal);
    ++ordinal;
    if (!mask_definition_affects_render(mask, ordinal - 1) ||
        pipeline_mask_tool_role(mask) != QStringLiteral("retouch")) {
      continue;
    }
    retouch_masks.push_back(pipeline_local_mask(mask));
  }
  return retouch_masks;
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
              .raw_white_balance_temperature = state.raw_white_balance_temperature,
              .raw_white_balance_tint = state.raw_white_balance_tint,
          },
      .color_pipeline = color_pipeline_config_for_state(state, metadata),
      .exposure = state.exposure,
      .contrast = state.contrast,
      .highlights = state.highlights,
      .shadows = state.shadows,
      .whites = state.whites,
      .blacks = state.blacks,
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
      .vibrance = state.vibrance,
      .saturation = state.saturation,
      .curve_shadows = state.curve_shadows,
      .curve_darks = state.curve_darks,
      .curve_lights = state.curve_lights,
      .curve_highlights = state.curve_highlights,
      .master_curve_points = pipeline_curve_points(state.master_curve_points),
      .red_curve_points = pipeline_curve_points(state.red_curve_points),
      .green_curve_points = pipeline_curve_points(state.green_curve_points),
      .blue_curve_points = pipeline_curve_points(state.blue_curve_points),
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
        }
        return channels;
      }(),
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

double channel_weight_for_selection(double hue, const QString& hsl_channel) {
  return photo_editor::pipeline::channel_weight_for_selection(hue, hsl_channel.toStdString());
}

double crop_aspect_ratio(const QString& preset) {
  if (preset == QStringLiteral("Square")) {
    return 1.0;
  }

  if (preset == QStringLiteral("4x5")) {
    return 4.0 / 5.0;
  }

  if (preset == QStringLiteral("3x2")) {
    return 3.0 / 2.0;
  }

  if (preset == QStringLiteral("16x9")) {
    return 16.0 / 9.0;
  }

  return 0.0;
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
         !state.raw_highlight_recovery ||
         !state.raw_gamut_mapping ||
         !state.raw_lens_correction ||
         state.raw_white_balance_override ||
         !qFuzzyCompare(state.raw_white_balance_temperature, 5200.0) ||
         !qFuzzyIsNull(state.raw_white_balance_tint) ||
         !qFuzzyIsNull(state.exposure) ||
         !qFuzzyIsNull(state.contrast) ||
         !qFuzzyIsNull(state.highlights) ||
         !qFuzzyIsNull(state.shadows) ||
         !qFuzzyIsNull(state.whites) ||
         !qFuzzyIsNull(state.blacks) ||
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
         std::abs(state.sharpening) > 1e-6 || std::abs(state.sharpen_masking) > 1e-6 ||
         std::abs(state.luminance_nr) > 1e-6 || std::abs(state.color_nr) > 1e-6 ||
         std::abs(state.lens_distortion) > 1e-6 || std::abs(state.lens_vignette) > 1e-6 ||
         std::abs(state.chromatic_aberration) > 1e-6;
}

bool requires_cpu_only_preview_pipeline(const EditState& state) {
  return std::abs(state.lens_distortion) > 1e-6 ||
         std::abs(state.lens_vignette) > 1e-6 ||
         std::abs(state.chromatic_aberration) > 1e-6;
}

bool has_extended_range_pixels(const std::vector<photo_editor::pipeline::ColorPixel>& pixels) {
  constexpr auto kTolerance = 1.0e-6F;
  return std::any_of(pixels.cbegin(), pixels.cend(), [](const auto& pixel) {
    return pixel.red > 1.0F + kTolerance || pixel.green > 1.0F + kTolerance ||
           pixel.blue > 1.0F + kTolerance || pixel.red < -kTolerance ||
           pixel.green < -kTolerance || pixel.blue < -kTolerance;
  });
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
  cached_state.raw_highlight_recovery = state.raw_highlight_recovery;
  cached_state.raw_gamut_mapping = state.raw_gamut_mapping;
  cached_state.raw_lens_correction = state.raw_lens_correction;
  cached_state.raw_white_balance_override = state.raw_white_balance_override;
  cached_state.raw_white_balance_temperature = state.raw_white_balance_temperature;
  cached_state.raw_white_balance_tint = state.raw_white_balance_tint;
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
    const RenderRequest* request) {
  const auto cache_key = request == nullptr ? QString{} : base_linear_cache_key(*request);
  if (cache_key.isEmpty()) {
    return linearize_preview_image(image, request);
  }

  {
    auto& cache = linear_image_cache();
    QMutexLocker locker(&cache.mutex);
    const auto cached = cache.images.constFind(cache_key);
    if (cached != cache.images.cend()) {
      cache.touch(cache_key);
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
  return linear_image;
}

bool decode_source_image(
    const QString& file_path,
    QImage* image,
    int max_edge,
    const EditState& state = {}) {
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
  if (should_cache) {
    auto& cache = decoded_image_cache();
    QMutexLocker locker(&cache.mutex);
    const auto cached = cache.images.constFind(cache_key);
    if (cached != cache.images.cend()) {
      *image = cached.value();
      cache.touch(cache_key);
      return !image->isNull();
    }
  }

  if (native_supported) {
    auto decoded = QImage{};
    if (!decode_native_image(file_path, &decoded, max_edge)) {
      return false;
    }

    *image = normalize_preview_color_space(std::move(decoded));
    if (should_cache && !image->isNull()) {
      auto& cache = decoded_image_cache();
      QMutexLocker locker(&cache.mutex);
      cache.insert(cache_key, *image);
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
      auto& cache = decoded_image_cache();
      QMutexLocker locker(&cache.mutex);
      cache.insert(cache_key, *image);
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
    auto& cache = decoded_image_cache();
    QMutexLocker locker(&cache.mutex);
    cache.insert(cache_key, *image);
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
    const RenderRequest* request);

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
  apply_output_color_space_transform(&overlay_image, preview_output_color_space_for_state(request.state));

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

bool apply_color_adjustments(QImage* image, const EditState& state, const RenderRequest* request = nullptr) {
  if (image == nullptr || image->isNull()) {
    return false;
  }

  const auto linear_source = cached_linear_preview_image(*image, request);
  if (!linear_source) {
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
  const auto requires_full_pipeline = requires_full_preview_pipeline(state);
  if (metal.available() &&
      pipeline_state.local_mask_sets.empty() &&
      pipeline_state.retouch_masks.empty() &&
      pipeline_state.local_masks.empty() &&
      !has_extended_range_pixels(pixels) &&
      !requires_full_pipeline) {
    if (request != nullptr && render_request_superseded(*request)) {
      return false;
    }
    const auto lut = photo_editor::pipeline::build_color_lut(pipeline_state);
    if (request != nullptr && render_request_superseded(*request)) {
      return false;
    }
    metal.apply_masks_and_color_lut(&pixels, width, height, pipeline_state, lut);
  } else {
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
      preview_target_color_space(),
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
  result.generation = request.generation;
  result.preview_overlay_rect = request.preview_overlay_rect;
  result.had_adjustments = has_visible_adjustments(request.state);

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
  QElapsedTimer decode_timer;
  decode_timer.start();
  if (!decode_source_image(request.file_path, &source_image, request.max_edge, request.state)) {
    result.status = QStringLiteral("Source file unavailable.");
    result.total_elapsed_ms = total_timer.elapsed();
    result.decode_elapsed_ms = decode_timer.elapsed();
    observability::logWarning(
        photoEditorPreviewLog(),
        QStringLiteral("preview.render.failed"),
        {
            {QStringLiteral("stage"), request.full_resolution ? QStringLiteral("full") : QStringLiteral("interactive")},
            {QStringLiteral("generation"), request.generation},
            {QStringLiteral("file_path"), request.file_path},
            {QStringLiteral("elapsed_ms"), result.total_elapsed_ms},
            {QStringLiteral("status"), result.status},
        });
    return result;
  }
  result.decode_elapsed_ms = decode_timer.elapsed();
  result.source_width = source_image.width();
  result.source_height = source_image.height();
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
    if (!apply_color_adjustments(&edited_image, request.state, &request) &&
        render_request_superseded(request)) {
      result.cancelled = true;
      result.total_elapsed_ms = total_timer.elapsed();
      result.edit_elapsed_ms = edit_timer.elapsed();
      return result;
    }
  }
  apply_output_color_space_transform(&edited_image, preview_output_color_space_for_state(request.state));
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
  if (request.use_in_memory_preview) {
    result.preview_image = edited_image;
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
    QImage reference_image = request.bypass_crop ? source_image : apply_crop(source_image, request.state);
    if (render_request_superseded(request)) {
      result.cancelled = true;
      return result;
    }
    apply_output_color_space_transform(&reference_image, preview_output_color_space_for_state(request.state));
    if (request.use_in_memory_preview) {
      result.reference_preview_image = reference_image;
      result.display_reference_preview_source =
          QStringLiteral("image://editor-preview/%1").arg(QString::fromLatin1(kReferencePreviewImageId));
    } else {
      const auto reference_output_path = cache_path_for_selection(
          request.file_path,
          request.state,
          false,
          request.full_resolution,
          request.bypass_crop,
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
QRectF EditorPreviewController::displayPreviewOverlayRect() const {
  return display_preview_overlay_rect_;
}
QVariantList EditorPreviewController::histogramRed() const { return histogram_red_; }
QVariantList EditorPreviewController::histogramGreen() const { return histogram_green_; }
QVariantList EditorPreviewController::histogramBlue() const { return histogram_blue_; }
QVariantList EditorPreviewController::histogramLuminance() const { return histogram_luminance_; }
int EditorPreviewController::histogramShadowClipCount() const { return histogram_shadow_clip_count_; }
int EditorPreviewController::histogramHighlightClipCount() const { return histogram_highlight_clip_count_; }
int EditorPreviewController::histogramSampleCount() const { return histogram_sample_count_; }
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
bool EditorPreviewController::processing() const { return processing_; }
bool EditorPreviewController::canUndo() const {
  return !asset_id_.isEmpty() && !asset_undo_states_.value(asset_id_).isEmpty();
}
bool EditorPreviewController::canRedo() const {
  return !asset_id_.isEmpty() && !asset_redo_states_.value(asset_id_).isEmpty();
}
bool EditorPreviewController::hasCopiedSettings() const { return has_copied_settings_; }
QStringList EditorPreviewController::presetNames() const { return presets_.keys(); }
bool EditorPreviewController::compareMode() const { return compare_mode_; }
bool EditorPreviewController::cropEditing() const { return crop_editing_; }
QString EditorPreviewController::outputColorSpace() const { return output_color_space_; }
bool EditorPreviewController::rawControlsAvailable() const { return raw_image_supported(file_path_); }
double EditorPreviewController::rawExposure() const { return raw_exposure_; }
double EditorPreviewController::rawBoost() const { return raw_boost_; }
double EditorPreviewController::rawShadowBoost() const { return raw_shadow_boost_; }
double EditorPreviewController::rawDetail() const { return raw_detail_; }
bool EditorPreviewController::rawHighlightRecovery() const { return raw_highlight_recovery_; }
bool EditorPreviewController::rawGamutMapping() const { return raw_gamut_mapping_; }
bool EditorPreviewController::rawLensCorrection() const { return raw_lens_correction_; }
bool EditorPreviewController::rawWhiteBalanceOverride() const { return raw_white_balance_override_; }
double EditorPreviewController::rawWhiteBalanceTemperature() const { return raw_white_balance_temperature_; }
double EditorPreviewController::rawWhiteBalanceTint() const { return raw_white_balance_tint_; }
double EditorPreviewController::exposure() const { return exposure_; }
double EditorPreviewController::contrast() const { return contrast_; }
double EditorPreviewController::highlights() const { return highlights_; }
double EditorPreviewController::shadows() const { return shadows_; }
double EditorPreviewController::whites() const { return whites_; }
double EditorPreviewController::blacks() const { return blacks_; }
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
double EditorPreviewController::vibrance() const { return vibrance_; }
double EditorPreviewController::saturation() const { return saturation_; }
double EditorPreviewController::curveShadows() const { return curve_shadows_; }
double EditorPreviewController::curveDarks() const { return curve_darks_; }
double EditorPreviewController::curveLights() const { return curve_lights_; }
double EditorPreviewController::curveHighlights() const { return curve_highlights_; }
QString EditorPreviewController::curveChannel() const { return curve_channel_; }
QVariantList EditorPreviewController::activeCurvePoints() const {
  const auto normalized_channel = curve_channel_.trimmed().isEmpty() ? QStringLiteral("Master") : curve_channel_;
  if (normalized_channel == QStringLiteral("Master")) {
    if (!master_curve_points_.isEmpty()) {
      return master_curve_points_;
    }
    if (!qFuzzyIsNull(curve_shadows_) || !qFuzzyIsNull(curve_darks_) ||
        !qFuzzyIsNull(curve_lights_) || !qFuzzyIsNull(curve_highlights_)) {
      return parametric_curve_points(curve_shadows_, curve_darks_, curve_lights_, curve_highlights_);
    }
    return default_curve_points();
  }
  if (normalized_channel == QStringLiteral("Red")) {
    return red_curve_points_.isEmpty() ? default_curve_points() : red_curve_points_;
  }
  if (normalized_channel == QStringLiteral("Green")) {
    return green_curve_points_.isEmpty() ? default_curve_points() : green_curve_points_;
  }
  return blue_curve_points_.isEmpty() ? default_curve_points() : blue_curve_points_;
}
QString EditorPreviewController::hslChannel() const { return hsl_channel_; }
double EditorPreviewController::hueAdjustment() const { return hue_adjustment_; }
double EditorPreviewController::saturationAdjustment() const { return saturation_adjustment_; }
double EditorPreviewController::luminanceAdjustment() const { return luminance_adjustment_; }
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
QVariantList EditorPreviewController::maskSets() const {
  if (mask_group_cache_dirty_) {
    cached_mask_sets_ = grouped_mask_sets(mask_stack_);
    cached_retouch_stack_ = grouped_retouch_stack(mask_stack_);
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
QVariantList EditorPreviewController::maskStack() const { return mask_stack_; }
int EditorPreviewController::maskCount() const { return static_cast<int>(mask_stack_.size()); }
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
  return map_bool_value(activeMaskDefinition(), QStringLiteral("selectionInvert"), false);
}
double EditorPreviewController::activeMaskSelectionExpand() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("selectionExpand"), 0.0);
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
  } else if (kind == QStringLiteral("luminance") || kind == QStringLiteral("color")) {
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
double EditorPreviewController::activeMaskRangeFeather() const {
  return map_double_value(activeMaskDefinition(), QStringLiteral("rangeFeather"), 0.1);
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
    renderPreview();
  }
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
    renderPreview();
  }
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
  renderPreview();
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

void EditorPreviewController::setRawHighlightRecovery(bool enabled) {
  if (raw_highlight_recovery_ == enabled) {
    return;
  }
  rememberStateForUndo();
  raw_highlight_recovery_ = enabled;
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
  const auto normalized =
      curve_channel.trimmed().isEmpty() ? QStringLiteral("Master") : curve_channel.trimmed();
  if (curve_channel_ == normalized) {
    return;
  }
  curve_channel_ = normalized;
  emit controlsChanged();
}

void EditorPreviewController::setActiveCurvePoints(const QVariantList& active_curve_points) {
  const auto normalized_points = normalize_curve_points(active_curve_points);
  auto* target_points = &blue_curve_points_;
  if (curve_channel_ == QStringLiteral("Master")) {
    target_points = &master_curve_points_;
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
  if (curve_channel_ == QStringLiteral("Master") && !master_curve_points_.isEmpty()) {
    curve_shadows_ = 0.0;
    curve_darks_ = 0.0;
    curve_lights_ = 0.0;
    curve_highlights_ = 0.0;
  }

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
    renderPreview();
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
    renderPreview();
  }
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
    renderPreview();
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
    renderPreview();
  }
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
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("selectionInvert"), selection_inverted);
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskSelectionExpand(double selection_expand) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty()) {
    return;
  }
  mask.insert(QStringLiteral("selectionExpand"), clamp_signed_percent(selection_expand));
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
  mask.insert(QStringLiteral("brushSize"), qBound(0.01, brush_size, 0.35));
  updateActiveMaskDefinition(mask);
}

void EditorPreviewController::setActiveMaskBrushFlow(double brush_flow) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("brush")) {
    return;
  }
  mask.insert(QStringLiteral("brushFlow"), qBound(0.05, brush_flow, 1.0));
  updateActiveMaskDefinition(mask);
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

void EditorPreviewController::setActiveMaskRangeFeather(double feather) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() ||
      (activeMaskKind() != QStringLiteral("luminance") && activeMaskKind() != QStringLiteral("color"))) {
    return;
  }
  mask.insert(QStringLiteral("rangeFeather"), qBound(0.0, feather, 0.5));
  updateActiveMaskDefinition(mask);
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
  emit assetChanged();
  renderPreview();
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
  vibrance_ = 0.0;
  saturation_ = 0.0;
  curve_shadows_ = 0.0;
  curve_darks_ = 0.0;
  curve_lights_ = 0.0;
  curve_highlights_ = 0.0;
  curve_channel_ = QStringLiteral("Master");
  master_curve_points_.clear();
  red_curve_points_.clear();
  green_curve_points_.clear();
  blue_curve_points_.clear();
  hsl_channel_ = QStringLiteral("Reds");
  hue_adjustment_ = 0.0;
  saturation_adjustment_ = 0.0;
  luminance_adjustment_ = 0.0;
  mixer_channels_ = {};
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
  input_color_profile_ = QStringLiteral("Embedded");
  working_color_profile_ = QStringLiteral("Linear ProPhoto RGB");
  preview_color_space_ = QStringLiteral("sRGB");
  render_intent_ = QStringLiteral("Perceptual");
  raw_input_transform_enabled_ = true;
  highlight_reconstruction_mode_ = QStringLiteral("Enabled");
  chromatic_adaptation_method_ = QStringLiteral("Bradford");
  output_color_space_ = QStringLiteral("sRGB");
  mask_stack_.clear();
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

QString EditorPreviewController::currentRecipeJson() const { return recipeJsonForState(currentState()); }

QString EditorPreviewController::copiedSettingsRecipeJson() const {
  if (!has_copied_settings_) {
    return {};
  }
  return recipeJsonForState(copied_settings_state_);
}

QString EditorPreviewController::presetRecipeJson(const QString& name) const {
  const auto normalized_name = normalizedPresetName(name);
  const auto iterator = presets_.constFind(normalized_name);
  if (iterator == presets_.cend()) {
    return {};
  }
  return recipeJsonForState(iterator.value());
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
  const auto normalized_name = normalizedPresetName(name);
  if (normalized_name.isEmpty() || (asset_id_.isEmpty() && !hasAsset())) {
    return false;
  }

  const auto preset_exists = presets_.contains(normalized_name);
  if (preset_exists && !overwrite) {
    return false;
  }

  const auto names_before = presets_.keys();
  presets_.insert(normalized_name, currentState());
  savePresets();
  if (names_before != presets_.keys()) {
    emit presetsChanged();
  }
  setStatus(!preset_exists
                ? QStringLiteral("Saved preset '%1'.").arg(normalized_name)
                : QStringLiteral("Updated preset '%1'.").arg(normalized_name));
  return true;
}

bool EditorPreviewController::applyPreset(const QString& name, const QStringList& adjustment_groups) {
  if (asset_id_.isEmpty() && !hasAsset()) {
    return false;
  }

  const auto normalized_name = normalizedPresetName(name);
  const auto iterator = presets_.constFind(normalized_name);
  if (iterator == presets_.cend()) {
    return false;
  }

  const auto merged_state = merge_edit_recipe_state_by_groups(currentState(), iterator.value(), adjustment_groups);
  if (edit_states_equal(merged_state, currentState())) {
    return false;
  }

  rememberStateForUndo();
  applyState(merged_state);
  persistCurrentState();
  renderPreview();
  setStatus(QStringLiteral("Applied preset '%1'.").arg(normalized_name));
  return true;
}

bool EditorPreviewController::deletePreset(const QString& name) {
  const auto normalized_name = normalizedPresetName(name);
  if (normalized_name.isEmpty() || !presets_.contains(normalized_name)) {
    return false;
  }

  presets_.remove(normalized_name);
  savePresets();
  emit presetsChanged();
  setStatus(QStringLiteral("Deleted preset '%1'.").arg(normalized_name));
  return true;
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

void EditorPreviewController::addSubjectMask() {
  if (file_path_.isEmpty()) {
    setStatus(QStringLiteral("Load an image before creating an AI subject mask."));
    return;
  }

  QImage source_image;
  if (!decodeSourceImage(&source_image, 1024)) {
    qWarning() << "addSubjectMask: failed to decode source image";
    setStatus(QStringLiteral("Unable to decode the source image for AI subject masking."));
    return;
  }

  const auto result = generate_subject_mask(source_image);
  if (!result.success) {
    qWarning() << "addSubjectMask:" << result.error_message;
    setStatus(
        result.error_message.isEmpty()
            ? QStringLiteral("AI subject masking is unavailable.")
            : result.error_message);
    return;
  }

  auto next_stack = mask_stack_;
  auto mask_def = default_mask_definition(QStringLiteral("subject"), static_cast<int>(next_stack.size()) + 1);
  if (hasActiveMask()) {
    const auto active_mask = activeMaskDefinition();
    if (is_selection_mask(active_mask)) {
      inherit_selection_mask_set(&mask_def, active_mask);
    }
  }
  mask_def.insert(QStringLiteral("feather"), 0.5);
  mask_def.insert(QStringLiteral("segmentationData"), result.mask_data);
  mask_def.insert(QStringLiteral("segmentationWidth"), result.width);
  mask_def.insert(QStringLiteral("segmentationHeight"), result.height);
  next_stack.append(mask_def);
  applyMaskStackChange(next_stack, static_cast<int>(next_stack.size()) - 1);
  setStatus(QStringLiteral("AI subject mask added."));
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

void EditorPreviewController::smartSelectObjectAt(
    double normalized_x,
    double normalized_y,
    bool create_new) {
  pickMaskAt(normalized_x, normalized_y, QStringLiteral("selection"), create_new);
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
  mask_def.insert(QStringLiteral("brushSize"), 0.06);
  mask_def.insert(QStringLiteral("brushFlow"), 1.0);
  mask_def.insert(QStringLiteral("feather"), 0.55);
  next_stack.insert(insert_index, mask_def);
  applyMaskStackChange(next_stack, insert_index);
  setStatus(
      subtract ? QStringLiteral("Erase Brush ready. Paint directly on the image to remove areas from the selection.")
               : QStringLiteral("Select Brush ready. Paint directly on the image to add areas to the selection."));
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
  mask_def.insert(QStringLiteral("brushSize"), 0.026);
  mask_def.insert(QStringLiteral("brushFlow"), 1.0);
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
  mask_def.insert(QStringLiteral("brushSize"), 0.08);
  mask_def.insert(QStringLiteral("brushFlow"), 0.92);
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
  mask_def.insert(QStringLiteral("brushSize"), 0.07);
  mask_def.insert(QStringLiteral("brushFlow"), 0.78);
  mask_def.insert(QStringLiteral("feather"), 0.80);
  next_stack.insert(insert_index, mask_def);
  applyMaskStackChange(next_stack, insert_index, false);
  setStatus(QStringLiteral("Heal Brush ready. Paint over blemishes and distractions, then apply."));
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
  mask_def.insert(QStringLiteral("brushSize"), 0.07);
  mask_def.insert(QStringLiteral("brushFlow"), 0.90);
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
  if (active_retouch_mode == QStringLiteral("remove") ||
      active_retouch_mode == QStringLiteral("heal")) {
    QImage source_image;
    if (decodeSourceImage(&source_image, 1024)) {
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

        const auto mask_data = mask.value(QStringLiteral("segmentationData")).toByteArray();
        const auto mask_width = mask.value(QStringLiteral("segmentationWidth")).toInt();
        const auto mask_height = mask.value(QStringLiteral("segmentationHeight")).toInt();
        if (!valid_segmentation_payload(mask_data, mask_width, mask_height) ||
            mask_width != source_image.width() || mask_height != source_image.height()) {
          continue;
        }

        merged_mask_data = union_segmentation_masks(
            merged_mask_data,
            merged_width,
            merged_height,
            mask_data,
            mask_width,
            mask_height);
        merged_width = mask_width;
        merged_height = mask_height;
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
        });
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
      active_retouch_mode == QStringLiteral("heal")) {
    if (has_inpaint_patch) {
      status += QStringLiteral(" AI inpaint patch baked.");
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

void EditorPreviewController::appendActivePolygonPoint(double normalized_x, double normalized_y) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("polygon")) {
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
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("polygon")) {
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
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("polygon")) {
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
  if (!hasActiveMask()) {
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
  if (!hasActiveMask() || offset == 0) {
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

  rememberStateForUndo();
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
            point.x,
            point.y,
            point.size,
            point.flow,
            activeMaskFeather());
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
  persistCurrentState();
  if (brush_stroke_needs_final_render_) {
    renderPreview();
  }
  brush_stroke_needs_final_render_ = false;
  emit activeBrushStrokeChanged();
}

void EditorPreviewController::addBrushPoint(double normalized_x, double normalized_y) {
  auto mask = activeMaskDefinition();
  if (mask.isEmpty() || activeMaskKind() != QStringLiteral("brush")) {
    return;
  }

  const auto brush_size = activeMaskBrushSize();
  const auto brush_flow = activeMaskBrushFlow();
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
          brush_flow);
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
          point.x,
          point.y,
          point.size,
          point.flow,
          activeMaskFeather());
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
        map_double_value(point, QStringLiteral("x"), 0.5),
        map_double_value(point, QStringLiteral("y"), 0.5),
        map_double_value(point, QStringLiteral("size"), activeMaskBrushSize()),
        map_double_value(point, QStringLiteral("flow"), activeMaskBrushFlow()),
        activeMaskFeather());
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

  if (rawControlsAvailable()) {
    setRawWhiteBalanceOverride(true);
    setRawWhiteBalanceTemperature(raw_white_balance_temperature_ + red_blue_balance * 1800.0);
    setRawWhiteBalanceTint(raw_white_balance_tint_ + green_balance * 100.0);
  } else {
    setTemperature(temperature_ + red_blue_balance * 55.0);
    setTint(tint_ + green_balance * 60.0);
  }
  setWhiteBalanceSampling(false);
  setStatus(
      QStringLiteral("White balance sampled from a %1 px patch.")
          .arg(patch_radius * 2 + 1));
}

void EditorPreviewController::renderPreview(bool schedule_full_resolution) {
  const auto now_msecs = QDateTime::currentMSecsSinceEpoch();
  const auto rapid_followup = last_render_request_msecs_ > 0 &&
                              now_msecs - last_render_request_msecs_ <=
                                  kInteractiveDraftBurstWindowMs;
  next_interactive_render_draft_ =
      rapid_followup || interactive_render_timer_.isActive() ||
      interactive_render_watcher_.isRunning() || interactive_request_pending_;
  last_render_request_msecs_ = now_msecs;
  ++render_generation_;
  latest_render_generation_->store(render_generation_, std::memory_order_relaxed);
  ++overlay_generation_;
  latest_overlay_generation_->store(overlay_generation_, std::memory_order_relaxed);
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
  const auto state = currentState();
  if (!next_interactive_render_draft_) {
    observability::logInfo(
        photoEditorPreviewLog(),
        QStringLiteral("preview.render.requested"),
        {
            {QStringLiteral("file_path"), file_path_},
            {QStringLiteral("has_adjustments"), has_visible_adjustments(state)},
            {QStringLiteral("schedule_full_resolution"), schedule_full_resolution},
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
    full_render_timer_.setInterval(full_render_debounce_ms_for_state(state));
    full_render_timer_.start();
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
      computeHistogram(result.histogram_image);
    }
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
}

void EditorPreviewController::startRenderStage(bool full_resolution, const RenderRequest& request) {
  auto& watcher = full_resolution ? full_render_watcher_ : interactive_render_watcher_;
  watcher.setFuture(QtConcurrent::run([request]() { return render_preview_request(request); }));
  updateProcessingState();
}

void EditorPreviewController::updateProcessingState() {
  const auto busy = interactive_render_timer_.isActive() ||
                    full_render_timer_.isActive() ||
                    interactive_render_watcher_.isRunning() ||
                    full_render_watcher_.isRunning() ||
                    interactive_request_pending_ ||
                    full_request_pending_;
  setProcessing(busy);
}

RenderRequest EditorPreviewController::currentRenderRequest(bool full_resolution) const {
  const auto state = currentState();
  const auto draft_interactive = !full_resolution && next_interactive_render_draft_;
  const auto max_edge =
      full_resolution ? 0
                      : (draft_interactive
                             ? draft_interactive_preview_edge_for_state(state, previewTargetEdge())
                             : interactive_preview_edge_for_state(state, previewTargetEdge()));
  return RenderRequest{
      .file_path = file_path_,
      .fallback_thumbnail_source = fallback_thumbnail_source_,
      .state = state,
      .compare_mode = compare_mode_,
      .bypass_crop = crop_editing_,
      .full_resolution = full_resolution,
      .max_edge = max_edge,
      .jpeg_quality = full_resolution ? 95 : (uses_expensive_interactive_preview(state) ? 86 : 90),
      .generation = render_generation_,
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
  applyState(stateFromRecipeJson(recipe_json));
  emit historyChanged();
}

void EditorPreviewController::applyState(const EditState& state) {
  raw_exposure_ = state.raw_exposure;
  raw_boost_ = state.raw_boost;
  raw_shadow_boost_ = state.raw_shadow_boost;
  raw_detail_ = state.raw_detail;
  raw_highlight_recovery_ = state.raw_highlight_recovery;
  raw_gamut_mapping_ = state.raw_gamut_mapping;
  raw_lens_correction_ = state.raw_lens_correction;
  raw_white_balance_override_ = state.raw_white_balance_override;
  raw_white_balance_temperature_ = state.raw_white_balance_temperature;
  raw_white_balance_tint_ = state.raw_white_balance_tint;
  exposure_ = state.exposure;
  contrast_ = state.contrast;
  highlights_ = state.highlights;
  shadows_ = state.shadows;
  whites_ = state.whites;
  blacks_ = state.blacks;
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
  vibrance_ = state.vibrance;
  saturation_ = state.saturation;
  curve_shadows_ = state.curve_shadows;
  curve_darks_ = state.curve_darks;
  curve_lights_ = state.curve_lights;
  curve_highlights_ = state.curve_highlights;
  master_curve_points_ = state.master_curve_points;
  red_curve_points_ = state.red_curve_points;
  green_curve_points_ = state.green_curve_points;
  blue_curve_points_ = state.blue_curve_points;
  hsl_channel_ = state.hsl_channel;
  hue_adjustment_ = state.hue_adjustment;
  saturation_adjustment_ = state.saturation_adjustment;
  luminance_adjustment_ = state.luminance_adjustment;
  for (std::size_t i = 0; i < 8; ++i) {
    mixer_channels_[i].hue = state.mixer_channels[i].hue;
    mixer_channels_[i].saturation = state.mixer_channels[i].saturation;
    mixer_channels_[i].luminance = state.mixer_channels[i].luminance;
  }
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
  input_color_profile_ = normalized_input_color_profile_name(state.input_color_profile);
  working_color_profile_ = normalized_working_color_profile_name(state.working_color_profile);
  preview_color_space_ = normalized_output_color_space_name(state.preview_color_space);
  render_intent_ = normalized_render_intent_name(state.render_intent);
  raw_input_transform_enabled_ = state.raw_input_transform_enabled;
  highlight_reconstruction_mode_ =
      normalized_highlight_reconstruction_mode_name(state.highlight_reconstruction_mode);
  chromatic_adaptation_method_ =
      normalized_chromatic_adaptation_method_name(state.chromatic_adaptation_method);
  output_color_space_ = normalized_output_color_space_name(state.output_color_space);
  mask_stack_ = normalize_mask_stack(state.mask_stack);
  invalidateMaskDerivedCaches();
  active_mask_index_ = -1;
  synchronizeMaskState();
  emit controlsChanged();
}

EditState EditorPreviewController::currentState() const {
  return EditState{
      .input_color_profile = input_color_profile_,
      .working_color_profile = working_color_profile_,
      .preview_color_space = preview_color_space_,
      .render_intent = render_intent_,
      .raw_input_transform_enabled = raw_input_transform_enabled_,
      .highlight_reconstruction_mode = highlight_reconstruction_mode_,
      .chromatic_adaptation_method = chromatic_adaptation_method_,
      .raw_exposure = raw_exposure_,
      .raw_boost = raw_boost_,
      .raw_shadow_boost = raw_shadow_boost_,
      .raw_detail = raw_detail_,
      .raw_highlight_recovery = raw_highlight_recovery_,
      .raw_gamut_mapping = raw_gamut_mapping_,
      .raw_lens_correction = raw_lens_correction_,
      .raw_white_balance_override = raw_white_balance_override_,
      .raw_white_balance_temperature = raw_white_balance_temperature_,
      .raw_white_balance_tint = raw_white_balance_tint_,
      .exposure = exposure_,
      .contrast = contrast_,
      .highlights = highlights_,
      .shadows = shadows_,
      .whites = whites_,
      .blacks = blacks_,
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
      .vibrance = vibrance_,
      .saturation = saturation_,
      .curve_shadows = curve_shadows_,
      .curve_darks = curve_darks_,
      .curve_lights = curve_lights_,
      .curve_highlights = curve_highlights_,
      .master_curve_points = master_curve_points_,
      .red_curve_points = red_curve_points_,
      .green_curve_points = green_curve_points_,
      .blue_curve_points = blue_curve_points_,
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
        }
        return channels;
      }(),
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
      .output_color_space = output_color_space_,
      .mask_stack = mask_stack_,
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
    renderPreview();
  }
  return true;
}

bool EditorPreviewController::updateMaskAt(int index, const QVariantMap& mask, bool rerender) {
  if (index < 0 || index >= mask_stack_.size()) {
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

bool EditorPreviewController::updateActiveMaskDefinition(const QVariantMap& mask, bool rerender) {
  return updateMaskAt(active_mask_index_, mask, rerender);
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

void EditorPreviewController::loadPresets() {
  presets_.clear();

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
    if (name.isEmpty() || recipe_json.trimmed().isEmpty()) {
      continue;
    }

    presets_.insert(name, stateFromRecipeJson(recipe_json));
  }
}

void EditorPreviewController::savePresets() const {
  const QFileInfo destination_info(presetsFilePath());
  QDir().mkpath(destination_info.dir().absolutePath());

  QJsonArray preset_array;
  for (auto iterator = presets_.cbegin(); iterator != presets_.cend(); ++iterator) {
    QJsonObject preset_object;
    preset_object.insert(QStringLiteral("name"), iterator.key());
    preset_object.insert(QStringLiteral("recipeJson"), recipeJsonForState(iterator.value()));
    preset_array.push_back(preset_object);
  }

  QJsonObject root;
  root.insert(QStringLiteral("presets"), preset_array);

  QSaveFile file(presetsFilePath());
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

QSize EditorPreviewController::activeBrushLivePreviewSize() const {
  const auto source_size = sourceImageSize();
  if (!source_size.isValid()) {
    return {};
  }
  const auto preview_edge =
      std::min(interactive_preview_edge_for_state(currentState(), previewTargetEdge()), 1024);
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
  if (useBuiltSaveRecipeBinary()) {
    return builtBinaryPath(QStringLiteral("save_edit_recipe"));
  }

  return QStringLiteral("cargo");
}

QStringList EditorPreviewController::saveRecipeArguments(const QString& asset_id, const QString& recipe_json) const {
  if (useBuiltSaveRecipeBinary()) {
    return {asset_id, recipe_json};
  }

  return {
      QStringLiteral("run"),
      QStringLiteral("--quiet"),
      QStringLiteral("--manifest-path"),
      QStringLiteral("rust/Cargo.toml"),
      QStringLiteral("-p"),
      QStringLiteral("photo-catalog"),
      QStringLiteral("--bin"),
      QStringLiteral("save_edit_recipe"),
      QStringLiteral("--"),
      asset_id,
      recipe_json,
  };
}

bool EditorPreviewController::useBuiltSaveRecipeBinary() const {
  const QFileInfo binary(builtBinaryPath(QStringLiteral("save_edit_recipe")));
  return binary.exists() && binary.isExecutable();
}

QString EditorPreviewController::builtBinaryPath(const QString& binary_name) const {
  return QDir::current().absoluteFilePath(QStringLiteral("rust/target/debug/%1").arg(binary_name));
}

void EditorPreviewController::computeHistogram(const QImage& image) {
  constexpr int kBins = 256;
  std::array<int, kBins> red_bins{};
  std::array<int, kBins> green_bins{};
  std::array<int, kBins> blue_bins{};
  std::array<int, kBins> luma_bins{};

  const auto w = image.width();
  const auto h = image.height();
  histogram_sample_count_ = std::max(0, w * h);
  histogram_shadow_clip_count_ = 0;
  histogram_highlight_clip_count_ = 0;
  for (int y = 0; y < h; ++y) {
    const auto* scanline = reinterpret_cast<const QRgb*>(image.constScanLine(y));
    for (int x = 0; x < w; ++x) {
      const auto pixel = scanline[x];
      const auto r = qRed(pixel);
      const auto g = qGreen(pixel);
      const auto b = qBlue(pixel);
      if (r == 0 || g == 0 || b == 0) {
        ++histogram_shadow_clip_count_;
      }
      if (r == 255 || g == 255 || b == 255) {
        ++histogram_highlight_clip_count_;
      }
      ++red_bins[static_cast<std::size_t>(r)];
      ++green_bins[static_cast<std::size_t>(g)];
      ++blue_bins[static_cast<std::size_t>(b)];
      // ITU-R BT.709 luminance
      const auto luma = static_cast<int>(0.2126 * r + 0.7152 * g + 0.0722 * b + 0.5);
      ++luma_bins[static_cast<std::size_t>(std::clamp(luma, 0, 255))];
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
    histogram_red_.clear();
    histogram_green_.clear();
    histogram_blue_.clear();
    histogram_luminance_.clear();
    emit histogramChanged();
    return;
  }

  const auto scale = 1.0 / static_cast<double>(max_count);
  QVariantList r_list, g_list, b_list, l_list;
  r_list.reserve(kBins);
  g_list.reserve(kBins);
  b_list.reserve(kBins);
  l_list.reserve(kBins);
  for (int i = 0; i < kBins; ++i) {
    r_list.append(static_cast<double>(red_bins[static_cast<std::size_t>(i)]) * scale);
    g_list.append(static_cast<double>(green_bins[static_cast<std::size_t>(i)]) * scale);
    b_list.append(static_cast<double>(blue_bins[static_cast<std::size_t>(i)]) * scale);
    l_list.append(static_cast<double>(luma_bins[static_cast<std::size_t>(i)]) * scale);
  }

  histogram_red_ = std::move(r_list);
  histogram_green_ = std::move(g_list);
  histogram_blue_ = std::move(b_list);
  histogram_luminance_ = std::move(l_list);
  emit histogramChanged();
}

}  // namespace photo_editor::desktop

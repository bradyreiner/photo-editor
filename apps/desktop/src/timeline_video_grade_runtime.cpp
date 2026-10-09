#include "timeline_video_grade_runtime.h"

#include "desktop_pipeline_state_compiler.h"
#include "edit_recipe_v13_adapter.h"
#include "edit_recipe_pipeline_bridge.h"
#include "edit_recipe_state.h"
#include "local_mask_compiler.h"
#include "photo_render_utils.h"
#include "photo_layer_compiler.h"

#include <QCryptographicHash>
#include <QColorSpace>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <qrgbafloat.h>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace photo_editor::desktop {
namespace {

constexpr int kVideoGradeLutFastSize = 33;
constexpr int kVideoGradeLutHighQualitySize = 65;
constexpr int kVideoGradeLutMaximumQualitySize = 97;
constexpr int kVideoGradeLutValidationSize = 23;
constexpr std::size_t kVideoGradeLutCacheEntries = 8U;
constexpr std::size_t kVideoGradeLutCacheBudgetBytes = 64U * 1024U * 1024U;
constexpr qint64 kVideoGradeLutDiskCacheBudgetBytes = 64LL * 1024LL * 1024LL;
constexpr double kVideoGradeLutP99ErrorLimit = 0.0015;
constexpr double kVideoGradeLutMaximumErrorLimit = 0.005;
constexpr int kRec709ColorProfileSampleCount = 4097;

struct PreparedVideoGrade final {
  EditRecipeState recipe;
  photo_editor::pipeline::ColorAdjustmentState pipeline_state;
  photo_editor::pipeline::RenderPlan render_plan;
};

struct GradeStateCacheEntry final {
  std::string key;
  std::shared_ptr<const EditRecipeState> recipe;
  std::shared_ptr<const photo_editor::pipeline::ColorAdjustmentState> state;
  std::shared_ptr<const photo_editor::pipeline::RenderPlan> render_plan;
  std::size_t byte_cost = 0U;
};

struct GradeStateCache final {
  std::mutex mutex;
  std::list<GradeStateCacheEntry> entries;
  std::unordered_map<
      std::string,
      std::list<GradeStateCacheEntry>::iterator> by_key;
  std::size_t resident_bytes = 0U;
  std::uint64_t hits = 0U;
  std::uint64_t misses = 0U;
  std::uint64_t evictions = 0U;
};

constexpr std::size_t kGradeStateCacheBudgetBytes = 64U * 1024U * 1024U;

bool near_zero(double value) {
  return std::isfinite(value) && std::abs(value) <= 1.0e-6;
}

bool pointwise_adjustment_supported(
    const photo_editor::pipeline::ColorAdjustmentState& state) {
  if (!near_zero(state.texture) || !near_zero(state.clarity) ||
      !near_zero(state.dehaze) || !near_zero(state.adaptive_contrast) ||
      !near_zero(state.sharpening) || !near_zero(state.sharpen_detail) ||
      !near_zero(state.sharpen_masking) || !near_zero(state.luminance_nr) ||
      !near_zero(state.color_nr) || !near_zero(state.lens_distortion) ||
      !near_zero(state.lens_vignette) || !near_zero(state.chromatic_aberration) ||
      !near_zero(state.perspective_vertical) ||
      !near_zero(state.perspective_horizontal) ||
      !near_zero(state.transform_rotation) ||
      std::abs(state.transform_scale - 1.0) > 1.0e-6 ||
      !near_zero(state.transform_skew_x) || !near_zero(state.transform_skew_y) ||
      !near_zero(state.liquify_strength) || state.content_aware_crop_fill ||
      !near_zero(state.hdr_local_tone_mapping) ||
      state.linear_mask_enabled || state.radial_mask_enabled ||
      !state.local_mask_sets.empty() || !state.retouch_masks.empty() ||
      !state.local_masks.empty()) {
    return false;
  }
  for (const auto& layer : state.photo_layers) {
    if (layer.kind != "adjustment" || !layer.mask_data.empty() ||
        layer.mask_width != 0 || layer.mask_height != 0 ||
        !layer.effects.empty() || !layer.effect_masks.empty() ||
        !layer.effect_error.empty() || !near_zero(layer.texture) ||
        !near_zero(layer.clarity) || !near_zero(layer.dehaze) ||
        !near_zero(layer.adaptive_contrast) || !near_zero(layer.sharpening) ||
        !near_zero(layer.sharpen_detail) || !near_zero(layer.sharpen_masking) ||
        !near_zero(layer.luminance_nr) || !near_zero(layer.color_nr) ||
        !near_zero(layer.lens_distortion) || !near_zero(layer.lens_vignette) ||
        !near_zero(layer.chromatic_aberration) ||
        !near_zero(layer.perspective_vertical) ||
        !near_zero(layer.perspective_horizontal) ||
        !near_zero(layer.global_transform_rotation) ||
        std::abs(layer.global_transform_scale - 1.0) > 1.0e-6 ||
        !near_zero(layer.transform_skew_x) || !near_zero(layer.transform_skew_y) ||
        !near_zero(layer.liquify_strength) || layer.content_aware_crop_fill) {
      return false;
    }
    if (layer.x != 0 || layer.y != 0 ||
        std::abs(layer.transform_scale - 1.0) > 1.0e-6 ||
        !near_zero(layer.transform_rotation) ||
        std::abs(layer.effect_scale_x - 1.0) > 1.0e-6 ||
        std::abs(layer.effect_scale_y - 1.0) > 1.0e-6 ||
        layer.effect_padding_x != 0 || layer.effect_padding_y != 0) {
      return false;
    }
  }
  return true;
}

std::optional<EditRecipeState> normalized_video_grade_state(
    const QString& recipe_json,
    QString* error) {
  const auto validation = validate_edit_recipe_json(recipe_json);
  if (!validation.valid) {
    if (error != nullptr) {
      *error = validation.errors.join(QStringLiteral(" "));
    }
    return std::nullopt;
  }

  auto recipe = parse_edit_recipe_json(recipe_json);
  if (recipe.camera_dcp_profile_asset.configured() ||
      recipe.input_icc_profile_asset.configured() ||
      recipe.preview_icc_profile_asset.configured() ||
      recipe.proof_icc_profile_asset.configured() ||
      recipe.export_icc_profile_asset.configured()) {
    if (error != nullptr) {
      *error = QStringLiteral(
          "Timeline video grade recipes do not accept photo-specific ICC or DCP profile assets.");
    }
    return std::nullopt;
  }
  for (const auto& value : recipe.layers) {
    const auto layer = value.toMap();
    const auto kind = layer.value(QStringLiteral("kind")).toString()
                          .trimmed().toLower();
    if (kind != QStringLiteral("adjustment")) {
      if (error != nullptr) {
        *error = QStringLiteral(
            "Timeline video grade recipes currently support photo-engine adjustment layers. Source, text, and pixel layers belong on timeline tracks.");
      }
      return std::nullopt;
    }
  }

  // A timeline clip owns the source transform. The recipe starts in the
  // normalized Rec.709 signal produced by that boundary and returns Rec.709.
  recipe.input_color_profile = QStringLiteral("Rec.709");
  recipe.preview_color_space = QStringLiteral("Rec.709");
  recipe.output_color_space = QStringLiteral("Rec.709");
  recipe.preview_output_mode = QStringLiteral("Match Export");
  recipe.monitor_color_profile.clear();
  recipe.soft_proof_enabled = false;
  recipe.preview_icc_profile_asset = {};

  return recipe;
}

std::optional<PreparedVideoGrade> prepare_video_grade(
    const QString& recipe_json,
    const QSize& output_size,
    QString* error) {
  auto recipe_result = normalized_video_grade_state(recipe_json, error);
  if (!recipe_result.has_value()) {
    return std::nullopt;
  }
  auto recipe = std::move(*recipe_result);
  auto color_pipeline = compile_color_pipeline_config(recipe);
  auto pipeline_state = compile_desktop_pipeline_state(
      recipe,
      nullptr,
      color_pipeline,
      DesktopRenderPurpose::Preview,
      LocalMaskRasterPolicy::RebuildEditableAtOutputResolution,
      output_size);
  if (!compiled_photo_layer_source_error(pipeline_state.photo_layers).isEmpty()) {
    if (error != nullptr) {
      *error = compiled_photo_layer_source_error(pipeline_state.photo_layers);
    }
    return std::nullopt;
  }
  const auto recipe_digest = QString::fromLatin1(
      QCryptographicHash::hash(
          serialize_edit_recipe_json(recipe).toUtf8(),
          QCryptographicHash::Sha256)
          .toHex());
  const auto plan_result = compile_desktop_render_plan_with_derived_assets(
      recipe,
      DesktopRenderPlanRequest{
          .recipe_id = QStringLiteral("timeline-video-grade:%1").arg(recipe_digest),
          .recipe_payload_digest = recipe_digest,
          .source_id = QStringLiteral("timeline-video-clip:%1").arg(recipe_digest),
          .decoder_provenance = QStringLiteral("timeline-qvideoframe-rec709-v1"),
          .raw_source = false,
          .source_width = static_cast<std::uint32_t>(output_size.width()),
          .source_height = static_cast<std::uint32_t>(output_size.height()),
          .rendering_target = photo_editor::pipeline::DisplayRenderingTarget::ActiveDisplay,
          .backend_requirement = photo_editor::pipeline::RenderBackendRequirement::GpuPermitted,
          .purpose = photo_editor::pipeline::RenderPurpose::InteractivePreview,
          .scope_domain = photo_editor::pipeline::RenderScopeDomain::FinalDisplay,
      });
  if (!plan_result.ok()) {
    QStringList errors;
    for (const auto& adapter_error : plan_result.adapter_errors) {
      errors.push_back(QString::fromStdString(adapter_error.message));
    }
    for (const auto& compiler_error : plan_result.compiler_errors) {
      errors.push_back(QString::fromStdString(compiler_error.message));
    }
    if (error != nullptr) {
      *error = errors.join(QStringLiteral(" "));
    }
    return std::nullopt;
  }
  return PreparedVideoGrade{
      std::move(recipe),
      std::move(pipeline_state),
      std::move(*plan_result.plan)};
}

GradeStateCache& grade_state_cache() {
  static GradeStateCache cache;
  return cache;
}

const QColorSpace& rec709_video_signal_color_space() {
  static const auto color_space = [] {
    QList<std::uint16_t> transfer_table;
    transfer_table.reserve(kRec709ColorProfileSampleCount);
    auto previous = std::uint16_t{0};
    for (int index = 0; index < kRec709ColorProfileSampleCount; ++index) {
      const auto encoded = static_cast<double>(index) /
                           static_cast<double>(kRec709ColorProfileSampleCount - 1);
      const auto linear = encoded < 0.081
                              ? encoded / 4.5
                              : std::pow((encoded + 0.099) / 1.099, 1.0 / 0.45);
      const auto sample = static_cast<std::uint16_t>(std::lround(
          std::clamp(linear, 0.0, 1.0) * 65535.0));
      previous = std::max(previous, sample);
      transfer_table.push_back(previous);
    }
    transfer_table.front() = 0;
    transfer_table.back() = std::numeric_limits<std::uint16_t>::max();
    auto result = QColorSpace(QColorSpace::Primaries::SRgb, transfer_table);
    result.setDescription(QStringLiteral("Rec.709 video signal"));
    return result;
  }();
  return color_space;
}

template <typename Value>
std::size_t vector_bytes(const std::vector<Value>& values) {
  return values.capacity() * sizeof(Value);
}

std::size_t local_mask_bytes(
    const photo_editor::pipeline::LocalMask& mask) {
  auto bytes = vector_bytes(mask.brush_points) +
               vector_bytes(mask.clone_strokes) +
               vector_bytes(mask.polygon_points) +
               vector_bytes(mask.depth_map) +
               vector_bytes(mask.segmentation_mask) +
               vector_bytes(mask.inpaint_patch_pixels);
  for (const auto& stroke : mask.clone_strokes) {
    bytes += vector_bytes(stroke.points);
  }
  return bytes;
}

std::size_t photo_layer_bytes(
    const photo_editor::pipeline::PhotoLayer& layer) {
  auto bytes = vector_bytes(layer.master_curve_points) +
               vector_bytes(layer.luma_curve_points) +
               vector_bytes(layer.red_curve_points) +
               vector_bytes(layer.green_curve_points) +
               vector_bytes(layer.blue_curve_points) +
               vector_bytes(layer.lut_table) +
               vector_bytes(layer.lut_stack) +
               vector_bytes(layer.lut_shaper_table) +
               vector_bytes(layer.mask_data) +
               vector_bytes(layer.pixels) +
               vector_bytes(layer.effects) +
               vector_bytes(layer.effect_masks);
  for (const auto& lut : layer.lut_stack) {
    bytes += vector_bytes(lut.table) + vector_bytes(lut.shaper_table);
  }
  for (const auto& mask : layer.effect_masks) {
    bytes += vector_bytes(mask.coverage);
  }
  return bytes;
}

std::size_t pipeline_state_bytes(
    const photo_editor::pipeline::ColorAdjustmentState& state) {
  auto bytes = sizeof(state) +
               vector_bytes(state.master_curve_points) +
               vector_bytes(state.luma_curve_points) +
               vector_bytes(state.red_curve_points) +
               vector_bytes(state.green_curve_points) +
               vector_bytes(state.blue_curve_points) +
               vector_bytes(state.lut_table) +
               vector_bytes(state.lut_stack) +
               vector_bytes(state.lut_shaper_table) +
               vector_bytes(state.local_mask_sets) +
               vector_bytes(state.retouch_masks) +
               vector_bytes(state.local_masks) +
               vector_bytes(state.photo_layers);
  for (const auto& lut : state.lut_stack) {
    bytes += vector_bytes(lut.table) + vector_bytes(lut.shaper_table);
  }
  for (const auto& mask : state.retouch_masks) {
    bytes += local_mask_bytes(mask);
  }
  for (const auto& mask : state.local_masks) {
    bytes += local_mask_bytes(mask);
  }
  for (const auto& layer : state.photo_layers) {
    bytes += photo_layer_bytes(layer);
  }
  return bytes;
}

void insert_grade_state_cache(
    const std::string& key,
    std::shared_ptr<const EditRecipeState> recipe,
    std::shared_ptr<const photo_editor::pipeline::ColorAdjustmentState> state,
    std::shared_ptr<const photo_editor::pipeline::RenderPlan> render_plan,
    std::size_t byte_cost) {
  if (byte_cost > kGradeStateCacheBudgetBytes) {
    return;
  }
  auto& cache = grade_state_cache();
  const std::lock_guard<std::mutex> lock(cache.mutex);
  const auto existing = cache.by_key.find(key);
  if (existing != cache.by_key.end()) {
    cache.resident_bytes -= existing->second->byte_cost;
    cache.entries.erase(existing->second);
    cache.by_key.erase(existing);
  }
  cache.entries.push_front(GradeStateCacheEntry{
      key,
      std::move(recipe),
      std::move(state),
      std::move(render_plan),
      byte_cost});
  cache.by_key.emplace(key, cache.entries.begin());
  cache.resident_bytes += byte_cost;
  while (cache.resident_bytes > kGradeStateCacheBudgetBytes &&
         !cache.entries.empty()) {
    const auto last = std::prev(cache.entries.end());
    cache.resident_bytes -= last->byte_cost;
    cache.by_key.erase(last->key);
    cache.entries.erase(last);
    ++cache.evictions;
  }
}

QString video_grade_cache_root() {
  const auto root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  return root.isEmpty()
             ? QString{}
             : QDir(root).filePath(QStringLiteral("timeline-video-grade-luts-v2"));
}

struct GradeLutCacheEntry final {
  QString key;
  std::shared_ptr<const photo_editor::pipeline::ColorLut3D> lut;
  QString file_path;
  int validation_sample_count = 0;
  double validation_p99_absolute_error = 0.0;
  double validation_maximum_absolute_error = 0.0;
};

struct GradeLutCache final {
  std::mutex mutex;
  std::list<GradeLutCacheEntry> entries;
  std::unordered_map<std::string, std::list<GradeLutCacheEntry>::iterator> by_key;
  std::size_t resident_table_bytes = 0U;
  std::uint64_t hits = 0U;
  std::uint64_t misses = 0U;
  std::uint64_t evictions = 0U;
};

std::size_t grade_lut_table_bytes(
    const std::shared_ptr<const photo_editor::pipeline::ColorLut3D>& lut) {
  return lut == nullptr
             ? 0U
             : lut->table.capacity() *
                   sizeof(photo_editor::pipeline::ColorPixel);
}

GradeLutCache& grade_lut_cache() {
  static GradeLutCache cache;
  return cache;
}

std::shared_ptr<const photo_editor::pipeline::ColorLut3D> remember_grade_lut(
    const QString& cache_key,
    std::shared_ptr<const photo_editor::pipeline::ColorLut3D> lut,
    const QString& file_path,
    int validation_sample_count,
    double validation_p99_absolute_error,
    double validation_maximum_absolute_error) {
  auto& cache = grade_lut_cache();
  const auto cache_key_std = cache_key.toStdString();
  const std::lock_guard<std::mutex> lock(cache.mutex);
  const auto found = cache.by_key.find(cache_key_std);
  if (found != cache.by_key.end()) {
    cache.entries.splice(cache.entries.begin(), cache.entries, found->second);
    return found->second->lut;
  }
  cache.entries.push_front(GradeLutCacheEntry{
      cache_key,
      std::move(lut),
      file_path,
      validation_sample_count,
      validation_p99_absolute_error,
      validation_maximum_absolute_error});
  cache.by_key.emplace(cache_key_std, cache.entries.begin());
  cache.resident_table_bytes += grade_lut_table_bytes(cache.entries.front().lut);
  while (cache.entries.size() > kVideoGradeLutCacheEntries ||
         cache.resident_table_bytes > kVideoGradeLutCacheBudgetBytes) {
    const auto last = std::prev(cache.entries.end());
    cache.resident_table_bytes -= grade_lut_table_bytes(last->lut);
    cache.by_key.erase(last->key.toStdString());
    cache.entries.erase(last);
    ++cache.evictions;
  }
  return cache.entries.front().lut;
}

bool write_grade_lut_file(
    const QString& file_path,
    const photo_editor::pipeline::ColorLut3D& lut,
    QString* error) {
  const auto serialized = photo_editor::pipeline::serialize_cube_lut(
      lut, "Photo Editor shared video grade");
  const auto bytes = QByteArray::fromStdString(serialized);
  QSaveFile file(file_path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
      !file.commit()) {
    if (error != nullptr) {
      *error = QStringLiteral("Cannot write the compiled timeline grade LUT.");
    }
    return false;
  }
  return true;
}

void prune_video_grade_lut_disk_cache(const QString& keep_file_path) {
  const auto directory_path = video_grade_cache_root();
  QDir directory(directory_path);
  if (directory_path.isEmpty() || !directory.exists()) {
    return;
  }
  const auto files = directory.entryInfoList(
      {QStringLiteral("*.cube")}, QDir::Files, QDir::Time);
  auto retained_bytes = qint64{0};
  for (const auto& info : files) {
    const auto keep_current = info.absoluteFilePath() == keep_file_path;
    if (keep_current || retained_bytes + info.size() <=
                            kVideoGradeLutDiskCacheBudgetBytes) {
      retained_bytes += info.size();
      continue;
    }
    QFile::remove(info.absoluteFilePath());
  }
}

struct GradeLutValidation final {
  bool accepted = false;
  double p99_absolute_error = 0.0;
  double maximum_absolute_error = 0.0;
  int sample_count = 0;
  QString error;
};

GradeLutValidation validate_grade_lut(
    const photo_editor::pipeline::ColorLut3D& lut,
    const QString& normalized_recipe_json,
    photo_editor::pipeline::CancellationToken cancellation) {
  const auto edge = kVideoGradeLutValidationSize;
  auto input = QImage(edge, edge * edge, QImage::Format_RGBA64);
  if (input.isNull()) {
    return {.error = QStringLiteral("Cannot allocate the grade LUT validation grid.")};
  }
  input.setColorSpace(rec709_video_signal_color_space());
  for (int red = 0; red < edge; ++red) {
    const auto red_code = static_cast<quint16>(std::lround(
        65535.0 * static_cast<double>(red) / static_cast<double>(edge - 1)));
    for (int green = 0; green < edge; ++green) {
      const auto green_code = static_cast<quint16>(std::lround(
          65535.0 * static_cast<double>(green) / static_cast<double>(edge - 1)));
      auto* row = reinterpret_cast<QRgba64*>(
          input.scanLine(red * edge + green));
      for (int blue = 0; blue < edge; ++blue) {
        const auto blue_code = static_cast<quint16>(std::lround(
            65535.0 * static_cast<double>(blue) / static_cast<double>(edge - 1)));
        row[blue] = QRgba64::fromRgba64(
            red_code, green_code, blue_code, 65535);
      }
    }
  }

  const auto exact = render_timeline_video_grade_frame(
      input,
      normalized_recipe_json,
      QStringLiteral("timeline-grade-lut-validation"),
      rec709_video_signal_color_space(),
      cancellation);
  if (!exact.ok()) {
    return {.error = exact.error.isEmpty()
                         ? QStringLiteral("The exact photo engine could not render the grade validation grid.")
                         : exact.error};
  }

  std::vector<double> errors;
  errors.reserve(static_cast<std::size_t>(edge) *
                 static_cast<std::size_t>(edge) *
                 static_cast<std::size_t>(edge));
  auto maximum_error = 0.0;
  for (int red = 0; red < edge; ++red) {
    if (cancellation.cancelled()) {
      return {.error = QStringLiteral("The shared grade LUT validation was cancelled.")};
    }
    for (int green = 0; green < edge; ++green) {
      const auto y = red * edge + green;
      const auto* input_row = reinterpret_cast<const QRgba64*>(
          input.constScanLine(y));
      const auto* exact_row = reinterpret_cast<const QRgba64*>(
          exact.image.constScanLine(y));
      for (int blue = 0; blue < edge; ++blue) {
        const auto& sample = input_row[blue];
        const auto input_pixel = photo_editor::pipeline::ColorPixel{
            .red = static_cast<float>(sample.red() / 65535.0),
            .green = static_cast<float>(sample.green() / 65535.0),
            .blue = static_cast<float>(sample.blue() / 65535.0),
            .alpha = 1.0F,
        };
        const auto approximated = photo_editor::pipeline::apply_lut_tetrahedral(
            lut, input_pixel);
        const auto& expected = exact_row[blue];
        const auto error = std::max({
            std::abs(static_cast<double>(approximated.red) - expected.red() / 65535.0),
            std::abs(static_cast<double>(approximated.green) - expected.green() / 65535.0),
            std::abs(static_cast<double>(approximated.blue) - expected.blue() / 65535.0),
        });
        if (!std::isfinite(error)) {
          return {.error = QStringLiteral("The shared grade LUT produced a non-finite validation error.")};
        }
        errors.push_back(error);
        maximum_error = std::max(maximum_error, error);
      }
    }
  }
  std::sort(errors.begin(), errors.end());
  const auto p99_index = static_cast<std::size_t>(std::ceil(
      static_cast<double>(errors.size()) * 0.99)) - 1U;
  const auto p99_error = errors[std::min(p99_index, errors.size() - 1U)];
  return {
      .accepted = p99_error <= kVideoGradeLutP99ErrorLimit &&
                  maximum_error <= kVideoGradeLutMaximumErrorLimit,
      .p99_absolute_error = p99_error,
      .maximum_absolute_error = maximum_error,
      .sample_count = static_cast<int>(errors.size()),
  };
}

struct GradeLutBuildResult final {
  std::shared_ptr<const photo_editor::pipeline::ColorLut3D> lut;
  QString error;
};

GradeLutBuildResult build_grade_lut_at_size(
    const QString& normalized_recipe_json,
    int size,
    photo_editor::pipeline::CancellationToken cancellation) {
  const auto height = size * size;
  const auto prepared = compile_timeline_video_grade_state(
      normalized_recipe_json, QSize(size, height));
  if (!prepared.ok()) {
    return {.error = prepared.error};
  }
  if (!pointwise_adjustment_supported(*prepared.state)) {
    return {.error = QStringLiteral(
        "This grade includes spatial masks or image-dependent operations. Delivery must use the shared frame graph before it can render this recipe.")};
  }
  if (cancellation.cancelled()) {
    return {.error = QStringLiteral("The shared grade LUT build was cancelled.")};
  }

  auto lattice = QImage(size, height, QImage::Format_RGBA32FPx4);
  if (lattice.isNull()) {
    return {.error = QStringLiteral("Cannot allocate the shared grade LUT sampling lattice.")};
  }
  lattice.setColorSpace(rec709_video_signal_color_space());
  for (int red = 0; red < size; ++red) {
    if (cancellation.cancelled()) {
      return {.error = QStringLiteral("The shared grade LUT build was cancelled.")};
    }
    const auto y = red * size;
    for (int green = 0; green < size; ++green) {
      auto* row = reinterpret_cast<QRgbaFloat32*>(lattice.scanLine(y + green));
      for (int blue = 0; blue < size; ++blue) {
        row[blue] = QRgbaFloat32{
            static_cast<float>(red) / static_cast<float>(size - 1),
            static_cast<float>(green) / static_cast<float>(size - 1),
            static_cast<float>(blue) / static_cast<float>(size - 1),
            1.0F,
        };
      }
    }
  }

  const auto rendered_lattice = render_output_image_with_compiled_state(
      lattice,
      *prepared.recipe,
      *prepared.state,
      rec709_video_signal_color_space(),
      QImage::Format_RGBA32FPx4,
      *prepared.render_plan,
      cancellation);
  if (rendered_lattice.isNull()) {
    return {.error = cancellation.cancelled()
                         ? QStringLiteral("The shared grade LUT build was cancelled.")
                         : QStringLiteral("The shared photo color engine could not compile the timeline grade LUT.")};
  }

  auto lut = std::make_shared<photo_editor::pipeline::ColorLut3D>();
  lut->table.reserve(
      static_cast<std::size_t>(size) *
      static_cast<std::size_t>(size) *
      static_cast<std::size_t>(size));
  for (int y = 0; y < rendered_lattice.height(); ++y) {
    if (cancellation.cancelled()) {
      return {.error = QStringLiteral("The shared grade LUT build was cancelled.")};
    }
    const auto* row = reinterpret_cast<const QRgbaFloat32*>(
        rendered_lattice.constScanLine(y));
    for (int x = 0; x < rendered_lattice.width(); ++x) {
      lut->table.push_back(photo_editor::pipeline::ColorPixel{
          .red = row[x].r,
          .green = row[x].g,
          .blue = row[x].b,
          .alpha = row[x].a,
      });
    }
  }
  lut->size = size;
  lut->input_color_space = "Rec.709";
  lut->output_color_space = "Rec.709";
  lut->valid = true;
  return {.lut = std::move(lut)};
}

}  // namespace

struct TimelineVideoGradeRenderSessionData final {
  std::shared_ptr<const EditRecipeState> recipe;
  std::shared_ptr<const photo_editor::pipeline::RenderPlan> render_plan;
  photo_editor::pipeline::ColorAdjustmentState pipeline_state;
  QString normalized_recipe_json;
  QString stable_clip_id;
  QSize output_size;
  std::mutex mutex;
};

namespace {

bool set_animated_grade_parameter(
    photo_editor::pipeline::ColorAdjustmentState* state,
    const QString& parameter,
    double value) {
  if (state == nullptr || !std::isfinite(value)) {
    return false;
  }
  if (parameter == QStringLiteral("exposure")) {
    state->input_transform.exposure = qBound(-5.0, value, 5.0);
  } else if (parameter == QStringLiteral("contrast")) {
    state->contrast = qBound(-100.0, value, 100.0);
  } else if (parameter == QStringLiteral("highlights")) {
    state->highlights = qBound(-100.0, value, 100.0);
  } else if (parameter == QStringLiteral("shadows")) {
    state->shadows = qBound(-100.0, value, 100.0);
  } else if (parameter == QStringLiteral("whites")) {
    state->whites = qBound(-100.0, value, 100.0);
  } else if (parameter == QStringLiteral("blacks")) {
    state->blacks = qBound(-100.0, value, 100.0);
  } else if (parameter == QStringLiteral("temperature")) {
    state->input_transform.temperature = qBound(-100.0, value, 100.0);
  } else if (parameter == QStringLiteral("tint")) {
    state->input_transform.tint = qBound(-100.0, value, 100.0);
  } else if (parameter == QStringLiteral("vibrance")) {
    state->vibrance = qBound(-100.0, value, 100.0);
  } else if (parameter == QStringLiteral("saturation")) {
    state->saturation = qBound(-100.0, value, 100.0);
  } else if (parameter == QStringLiteral("gradingShadowsHue")) {
    state->grading_shadows_hue = qBound(0.0, value, 1.0);
  } else if (parameter == QStringLiteral("gradingShadowsSaturation")) {
    state->grading_shadows_saturation = qBound(0.0, value, 100.0);
  } else if (parameter == QStringLiteral("gradingShadowsLuminance")) {
    state->grading_shadows_luminance = qBound(-100.0, value, 100.0);
  } else if (parameter == QStringLiteral("gradingMidtonesHue")) {
    state->grading_midtones_hue = qBound(0.0, value, 1.0);
  } else if (parameter == QStringLiteral("gradingMidtonesSaturation")) {
    state->grading_midtones_saturation = qBound(0.0, value, 100.0);
  } else if (parameter == QStringLiteral("gradingMidtonesLuminance")) {
    state->grading_midtones_luminance = qBound(-100.0, value, 100.0);
  } else if (parameter == QStringLiteral("gradingHighlightsHue")) {
    state->grading_highlights_hue = qBound(0.0, value, 1.0);
  } else if (parameter == QStringLiteral("gradingHighlightsSaturation")) {
    state->grading_highlights_saturation = qBound(0.0, value, 100.0);
  } else if (parameter == QStringLiteral("gradingHighlightsLuminance")) {
    state->grading_highlights_luminance = qBound(-100.0, value, 100.0);
  } else {
    return false;
  }
  return true;
}

bool set_animated_layer_grade_parameter(
    photo_editor::pipeline::PhotoLayer* layer,
    const QString& parameter,
    double value) {
  if (layer == nullptr || !std::isfinite(value)) {
    return false;
  }
  if (parameter == QStringLiteral("opacity")) {
    layer->opacity = qBound(0.0, value, 1.0);
  } else if (parameter == QStringLiteral("exposure")) {
    layer->exposure = qBound(-5.0, value, 5.0);
  } else if (parameter == QStringLiteral("contrast")) {
    layer->contrast = qBound(-100.0, value, 100.0);
  } else if (parameter == QStringLiteral("highlights")) {
    layer->highlights = qBound(-100.0, value, 100.0);
  } else if (parameter == QStringLiteral("shadows")) {
    layer->shadows = qBound(-100.0, value, 100.0);
  } else if (parameter == QStringLiteral("whites")) {
    layer->whites = qBound(-100.0, value, 100.0);
  } else if (parameter == QStringLiteral("blacks")) {
    layer->blacks = qBound(-100.0, value, 100.0);
  } else if (parameter == QStringLiteral("temperature")) {
    layer->temperature = qBound(-100.0, value, 100.0);
  } else if (parameter == QStringLiteral("tint")) {
    layer->tint = qBound(-100.0, value, 100.0);
  } else if (parameter == QStringLiteral("vibrance")) {
    layer->vibrance = qBound(-100.0, value, 100.0);
  } else if (parameter == QStringLiteral("saturation")) {
    layer->saturation = qBound(-100.0, value, 100.0);
  } else if (parameter == QStringLiteral("gradingShadowsHue")) {
    layer->grading_shadows_hue = qBound(0.0, value, 1.0);
  } else if (parameter == QStringLiteral("gradingShadowsSaturation")) {
    layer->grading_shadows_saturation = qBound(0.0, value, 100.0);
  } else if (parameter == QStringLiteral("gradingShadowsLuminance")) {
    layer->grading_shadows_luminance = qBound(-100.0, value, 100.0);
  } else if (parameter == QStringLiteral("gradingMidtonesHue")) {
    layer->grading_midtones_hue = qBound(0.0, value, 1.0);
  } else if (parameter == QStringLiteral("gradingMidtonesSaturation")) {
    layer->grading_midtones_saturation = qBound(0.0, value, 100.0);
  } else if (parameter == QStringLiteral("gradingMidtonesLuminance")) {
    layer->grading_midtones_luminance = qBound(-100.0, value, 100.0);
  } else if (parameter == QStringLiteral("gradingHighlightsHue")) {
    layer->grading_highlights_hue = qBound(0.0, value, 1.0);
  } else if (parameter == QStringLiteral("gradingHighlightsSaturation")) {
    layer->grading_highlights_saturation = qBound(0.0, value, 100.0);
  } else if (parameter == QStringLiteral("gradingHighlightsLuminance")) {
    layer->grading_highlights_luminance = qBound(-100.0, value, 100.0);
  } else {
    return false;
  }
  return true;
}

double layer_grade_base_value(
    const QVariantMap& layer,
    const QString& parameter) {
  const auto zones = layer.value(QStringLiteral("colorGrading")).toMap();
  if (parameter.startsWith(QStringLiteral("gradingShadows"))) {
    const auto channel = parameter.mid(QStringLiteral("gradingShadows").size()).toLower();
    return zones.value(QStringLiteral("shadows")).toMap().value(channel).toDouble();
  }
  if (parameter.startsWith(QStringLiteral("gradingMidtones"))) {
    const auto channel = parameter.mid(QStringLiteral("gradingMidtones").size()).toLower();
    return zones.value(QStringLiteral("midtones")).toMap().value(channel).toDouble();
  }
  if (parameter.startsWith(QStringLiteral("gradingHighlights"))) {
    const auto channel = parameter.mid(QStringLiteral("gradingHighlights").size()).toLower();
    return zones.value(QStringLiteral("highlights")).toMap().value(channel).toDouble();
  }
  const auto adjustments = layer.value(QStringLiteral("adjustments")).toMap();
  return layer.contains(parameter)
             ? layer.value(parameter).toDouble()
             : adjustments.value(parameter).toDouble();
}

void reset_animated_layer_grade_parameters(
    photo_editor::pipeline::ColorAdjustmentState* state,
    const EditRecipeState& recipe) {
  if (state == nullptr) {
    return;
  }
  static const auto parameters = QStringList{
      QStringLiteral("opacity"),
      QStringLiteral("exposure"), QStringLiteral("contrast"),
      QStringLiteral("highlights"), QStringLiteral("shadows"),
      QStringLiteral("whites"), QStringLiteral("blacks"),
      QStringLiteral("temperature"), QStringLiteral("tint"),
      QStringLiteral("vibrance"), QStringLiteral("saturation"),
      QStringLiteral("gradingShadowsHue"),
      QStringLiteral("gradingShadowsSaturation"),
      QStringLiteral("gradingShadowsLuminance"),
      QStringLiteral("gradingMidtonesHue"),
      QStringLiteral("gradingMidtonesSaturation"),
      QStringLiteral("gradingMidtonesLuminance"),
      QStringLiteral("gradingHighlightsHue"),
      QStringLiteral("gradingHighlightsSaturation"),
      QStringLiteral("gradingHighlightsLuminance")};
  for (const auto& value : recipe.layers) {
    const auto layer_values = value.toMap();
    const auto layer_id = layer_values.value(QStringLiteral("id")).toString();
    const auto layer = std::find_if(
        state->photo_layers.begin(), state->photo_layers.end(),
        [&layer_id](const auto& candidate) {
          return candidate.id == layer_id.toStdString();
        });
    if (layer == state->photo_layers.end()) {
      continue;
    }
    for (const auto& parameter : parameters) {
      (void)set_animated_layer_grade_parameter(
          &*layer, parameter, layer_grade_base_value(layer_values, parameter));
    }
  }
}

bool apply_animated_grade_parameters(
    photo_editor::pipeline::ColorAdjustmentState* state,
    const EditRecipeState& recipe,
    const QVariantMap& animated_parameters) {
  const auto base_values = QVariantMap{
      {QStringLiteral("exposure"), recipe.exposure},
      {QStringLiteral("contrast"), recipe.contrast},
      {QStringLiteral("highlights"), recipe.highlights},
      {QStringLiteral("shadows"), recipe.shadows},
      {QStringLiteral("whites"), recipe.whites},
      {QStringLiteral("blacks"), recipe.blacks},
      {QStringLiteral("temperature"), recipe.temperature},
      {QStringLiteral("tint"), recipe.tint},
      {QStringLiteral("vibrance"), recipe.vibrance},
      {QStringLiteral("saturation"), recipe.saturation},
      {QStringLiteral("gradingShadowsHue"), recipe.grading_shadows_hue},
      {QStringLiteral("gradingShadowsSaturation"), recipe.grading_shadows_saturation},
      {QStringLiteral("gradingShadowsLuminance"), recipe.grading_shadows_luminance},
      {QStringLiteral("gradingMidtonesHue"), recipe.grading_midtones_hue},
      {QStringLiteral("gradingMidtonesSaturation"), recipe.grading_midtones_saturation},
      {QStringLiteral("gradingMidtonesLuminance"), recipe.grading_midtones_luminance},
      {QStringLiteral("gradingHighlightsHue"), recipe.grading_highlights_hue},
      {QStringLiteral("gradingHighlightsSaturation"), recipe.grading_highlights_saturation},
      {QStringLiteral("gradingHighlightsLuminance"), recipe.grading_highlights_luminance},
  };
  reset_animated_layer_grade_parameters(state, recipe);
  for (auto it = base_values.cbegin(); it != base_values.cend(); ++it) {
    if (!set_animated_grade_parameter(state, it.key(), it.value().toDouble())) {
      return false;
    }
  }
  for (auto it = animated_parameters.cbegin(); it != animated_parameters.cend(); ++it) {
    const auto parameter = it.key();
    if (parameter.startsWith(QStringLiteral("layer:"))) {
      const auto suffix = parameter.mid(6);
      const auto separator = suffix.indexOf(QLatin1Char(':'));
      if (separator <= 0 || separator >= suffix.size() - 1) {
        return false;
      }
      const auto layer_id = suffix.left(separator).toStdString();
      const auto layer = std::find_if(
          state->photo_layers.begin(), state->photo_layers.end(),
          [&layer_id](const auto& candidate) { return candidate.id == layer_id; });
      if (layer == state->photo_layers.end()) {
        return false;
      }
      if (!set_animated_layer_grade_parameter(
              &*layer, suffix.mid(separator + 1), it.value().toDouble())) {
        return false;
      }
    } else if (!set_animated_grade_parameter(
                   state, parameter, it.value().toDouble())) {
      return false;
    }
  }
  return true;
}

}  // namespace

const QColorSpace& timeline_video_rec709_color_space() {
  return rec709_video_signal_color_space();
}

QString normalize_timeline_video_grade_recipe(
    const QString& recipe_json,
    QString* normalized_recipe_json) {
  if (normalized_recipe_json == nullptr) {
    return QStringLiteral("A destination is required for the normalized recipe.");
  }
  QString error;
  const auto recipe = normalized_video_grade_state(recipe_json, &error);
  if (!recipe.has_value()) {
    return error.isEmpty() ? QStringLiteral("The video grade recipe is invalid.") : error;
  }
  *normalized_recipe_json = serialize_edit_recipe_json(*recipe);
  return {};
}

TimelineVideoGradeState compile_timeline_video_grade_state(
    const QString& normalized_recipe_json,
    const QSize& output_size) {
  if (normalized_recipe_json.trimmed().isEmpty()) {
    return {.error = QStringLiteral("The timeline video grade recipe is empty.")};
  }
  if (!output_size.isValid() || output_size.isEmpty() ||
      output_size.width() > 8192 || output_size.height() > 8192) {
    return {.error = QStringLiteral("The timeline grade output dimensions are invalid or exceed the supported limit.")};
  }
  const auto recipe_hash = QCryptographicHash::hash(
      QByteArrayLiteral("timeline-video-grade-state-v2\n") +
          normalized_recipe_json.toUtf8() + '\n' +
          QByteArray::number(output_size.width()) + 'x' +
          QByteArray::number(output_size.height()),
      QCryptographicHash::Sha256);
  const auto key = QString::fromLatin1(recipe_hash.toHex()).toStdString();
  auto& cache = grade_state_cache();
  {
    const std::lock_guard<std::mutex> lock(cache.mutex);
    const auto found = cache.by_key.find(key);
    if (found != cache.by_key.end()) {
      ++cache.hits;
      auto state = found->second->state;
      auto render_plan = found->second->render_plan;
      auto recipe = found->second->recipe;
      cache.entries.splice(cache.entries.begin(), cache.entries, found->second);
      return {
          .recipe = std::move(recipe),
          .state = std::move(state),
          .render_plan = std::move(render_plan),
      };
    }
    ++cache.misses;
  }

  QString error;
  auto prepared = prepare_video_grade(normalized_recipe_json, output_size, &error);
  if (!prepared.has_value()) {
    return {.error = error};
  }
  auto recipe = std::make_shared<const EditRecipeState>(
      std::move(prepared->recipe));
  auto state = std::make_shared<const photo_editor::pipeline::ColorAdjustmentState>(
      prepared->pipeline_state);
  auto render_plan = std::make_shared<const photo_editor::pipeline::RenderPlan>(
      prepared->render_plan);
  const auto recipe_payload_bytes =
      static_cast<std::size_t>(normalized_recipe_json.size()) * sizeof(QChar);
  insert_grade_state_cache(
      key,
      recipe,
      state,
      render_plan,
      pipeline_state_bytes(*state) + recipe_payload_bytes);
  return {
      .recipe = std::move(recipe),
      .state = std::move(state),
      .render_plan = std::move(render_plan),
  };
}

TimelineVideoGradeFrame render_timeline_video_grade_frame(
    const QImage& rec709_frame,
    const QString& normalized_recipe_json,
    const QString& stable_clip_id,
    const QColorSpace& target_color_space,
    photo_editor::pipeline::CancellationToken cancellation) {
  if (rec709_frame.isNull() || stable_clip_id.trimmed().isEmpty()) {
    return {.error = QStringLiteral("A Rec.709 frame and stable clip id are required for shared grading.")};
  }
  if (cancellation.cancelled()) {
    return {.error = QStringLiteral("The shared grade frame was cancelled.")};
  }
  const auto compiled = compile_timeline_video_grade_state(
      normalized_recipe_json, rec709_frame.size());
  if (!compiled.ok()) {
    return {.error = compiled.error};
  }
  auto input = rec709_frame;
  if (!input.colorSpace().isValid()) {
    input.setColorSpace(rec709_video_signal_color_space());
  }
  auto rendered = render_output_image_with_compiled_state(
      input,
      *compiled.recipe,
      *compiled.state,
      target_color_space,
      rec709_frame.format() == QImage::Format_RGBA64
          ? QImage::Format_RGBA64
          : QImage::Format_RGBA8888,
      *compiled.render_plan,
      cancellation);
  if (rendered.isNull()) {
    return {.error = cancellation.cancelled()
                         ? QStringLiteral("The shared grade frame was cancelled.")
                         : QStringLiteral("The shared photo color pipeline could not render this video frame.")};
  }
  return {.image = std::move(rendered)};
}

TimelineVideoGradeRenderSession create_timeline_video_grade_render_session(
    const QString& normalized_recipe_json,
    const QString& stable_clip_id,
    const QSize& output_size) {
  if (stable_clip_id.trimmed().isEmpty()) {
    return {.error = QStringLiteral("A stable clip id is required for a shared grade render session.")};
  }
  const auto compiled = compile_timeline_video_grade_state(
      normalized_recipe_json, output_size);
  if (!compiled.ok()) {
    return {.error = compiled.error};
  }
  auto data = std::make_shared<TimelineVideoGradeRenderSessionData>();
  data->recipe = compiled.recipe;
  data->render_plan = compiled.render_plan;
  data->pipeline_state = *compiled.state;
  data->normalized_recipe_json = normalized_recipe_json;
  data->stable_clip_id = stable_clip_id.trimmed();
  data->output_size = output_size;
  return {.data = std::move(data)};
}

TimelineVideoGradeFrame render_timeline_video_grade_frame(
    const QImage& rec709_frame,
    TimelineVideoGradeRenderSession& session,
    const QVariantMap& animated_parameters,
    const QColorSpace& target_color_space,
    photo_editor::pipeline::CancellationToken cancellation) {
  if (rec709_frame.isNull() || !session.ok()) {
    return {.error = session.error.isEmpty()
                         ? QStringLiteral("A valid Rec.709 frame and shared grade session are required.")
                         : session.error};
  }
  if (cancellation.cancelled()) {
    return {.error = QStringLiteral("The shared grade frame was cancelled.")};
  }
  const std::lock_guard<std::mutex> lock(session.data->mutex);
  if (rec709_frame.size() != session.data->output_size) {
    const auto compiled = compile_timeline_video_grade_state(
        session.data->normalized_recipe_json, rec709_frame.size());
    if (!compiled.ok()) {
      return {.error = compiled.error};
    }
    session.data->recipe = compiled.recipe;
    session.data->render_plan = compiled.render_plan;
    session.data->pipeline_state = *compiled.state;
    session.data->output_size = rec709_frame.size();
  }
  if (!apply_animated_grade_parameters(
          &session.data->pipeline_state,
          *session.data->recipe,
          animated_parameters)) {
    return {.error = QStringLiteral("An animated shared grade parameter is unsupported or non-finite.")};
  }
  auto input = rec709_frame;
  if (!input.colorSpace().isValid()) {
    input.setColorSpace(rec709_video_signal_color_space());
  }
  auto rendered = render_output_image_with_compiled_state(
      input,
      *session.data->recipe,
      session.data->pipeline_state,
      target_color_space,
      rec709_frame.format() == QImage::Format_RGBA64
          ? QImage::Format_RGBA64
          : QImage::Format_RGBA8888,
      *session.data->render_plan,
      cancellation);
  if (rendered.isNull()) {
    return {.error = cancellation.cancelled()
                         ? QStringLiteral("The shared grade frame was cancelled.")
                         : QStringLiteral("The shared photo color pipeline could not render this video frame.")};
  }
  return {.image = std::move(rendered)};
}

bool timeline_video_grade_can_use_lut(
    const QString& normalized_recipe_json,
    const QSize& output_size,
    QString* error) {
  const auto compiled = compile_timeline_video_grade_state(
      normalized_recipe_json, output_size);
  if (!compiled.ok()) {
    if (error != nullptr) {
      *error = compiled.error;
    }
    return false;
  }
  if (!pointwise_adjustment_supported(*compiled.state)) {
    if (error != nullptr) {
      *error = QStringLiteral(
          "This grade contains masks or image-dependent operations and will use the shared frame renderer.");
    }
    return false;
  }
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

TimelineVideoGradeLut compile_timeline_video_grade_lut(
    const QString& normalized_recipe_json,
    photo_editor::pipeline::CancellationToken cancellation) {
  if (normalized_recipe_json.trimmed().isEmpty()) {
    return {.error = QStringLiteral("The timeline video grade recipe is empty.")};
  }
  const auto recipe_hash = QCryptographicHash::hash(
      QByteArrayLiteral("timeline-video-grade-v2\n") +
          normalized_recipe_json.toUtf8(),
      QCryptographicHash::Sha256);
  const auto cache_key = QString::fromLatin1(recipe_hash.toHex());
  const auto file_path = QDir(video_grade_cache_root()).filePath(
      cache_key + QStringLiteral(".cube"));
  auto& cache = grade_lut_cache();
  const auto cache_key_std = cache_key.toStdString();
  {
    const std::lock_guard<std::mutex> lock(cache.mutex);
    const auto found = cache.by_key.find(cache_key_std);
    if (found != cache.by_key.end()) {
      ++cache.hits;
      auto lut = found->second->lut;
      const auto validation_sample_count = found->second->validation_sample_count;
      const auto validation_p99_absolute_error =
          found->second->validation_p99_absolute_error;
      const auto validation_maximum_absolute_error =
          found->second->validation_maximum_absolute_error;
      cache.entries.splice(cache.entries.begin(), cache.entries, found->second);
      if (QFileInfo(file_path).isReadable() || write_grade_lut_file(
              file_path, *lut, nullptr)) {
        return {
            .lut = std::move(lut),
            .file_path = file_path,
            .size = found->second->lut->size,
            .validation_sample_count = validation_sample_count,
            .validation_p99_absolute_error = validation_p99_absolute_error,
            .validation_maximum_absolute_error = validation_maximum_absolute_error,
        };
      }
      const auto stale_entry = found->second;
      cache.resident_table_bytes -= grade_lut_table_bytes(stale_entry->lut);
      cache.by_key.erase(found);
      cache.entries.erase(stale_entry);
      ++cache.evictions;
    }
    ++cache.misses;
  }
  if (QFileInfo(file_path).isReadable()) {
    auto parsed = photo_editor::pipeline::load_cube_lut_file(
        file_path.toStdString());
    const auto size = parsed.lut.size;
    const auto expected_sample_count = static_cast<std::size_t>(size) *
                                       static_cast<std::size_t>(size) *
                                       static_cast<std::size_t>(size);
    if (parsed.ok() &&
        (size == kVideoGradeLutFastSize ||
         size == kVideoGradeLutHighQualitySize ||
         size == kVideoGradeLutMaximumQualitySize) &&
        parsed.lut.table.size() == expected_sample_count) {
      const auto validation = validate_grade_lut(
          parsed.lut, normalized_recipe_json, cancellation);
      if (!validation.error.isEmpty()) {
        return {.error = validation.error};
      }
      if (validation.accepted) {
        auto shared_lut = std::make_shared<const photo_editor::pipeline::ColorLut3D>(
            std::move(parsed.lut));
        shared_lut = remember_grade_lut(
            cache_key,
            std::move(shared_lut),
            file_path,
            validation.sample_count,
            validation.p99_absolute_error,
            validation.maximum_absolute_error);
        return {
            .lut = std::move(shared_lut),
            .file_path = file_path,
            .size = size,
            .validation_sample_count = validation.sample_count,
            .validation_p99_absolute_error = validation.p99_absolute_error,
            .validation_maximum_absolute_error = validation.maximum_absolute_error,
        };
      }
    }
  }
  if (cancellation.cancelled()) {
    return {.error = QStringLiteral("The shared grade LUT build was cancelled.")};
  }

  auto built = build_grade_lut_at_size(
      normalized_recipe_json, kVideoGradeLutFastSize, cancellation);
  if (!built.lut) {
    return {.error = built.error};
  }
  auto validation = validate_grade_lut(
      *built.lut, normalized_recipe_json, cancellation);
  if (!validation.error.isEmpty()) {
    return {.error = validation.error};
  }
  if (!validation.accepted) {
    built = build_grade_lut_at_size(
        normalized_recipe_json, kVideoGradeLutHighQualitySize, cancellation);
    if (!built.lut) {
      return {.error = built.error};
    }
    validation = validate_grade_lut(
        *built.lut, normalized_recipe_json, cancellation);
    if (!validation.error.isEmpty()) {
      return {.error = validation.error};
    }
    if (!validation.accepted) {
      built = build_grade_lut_at_size(
          normalized_recipe_json, kVideoGradeLutMaximumQualitySize, cancellation);
      if (!built.lut) {
        return {.error = built.error};
      }
      validation = validate_grade_lut(
          *built.lut, normalized_recipe_json, cancellation);
      if (!validation.error.isEmpty()) {
        return {.error = validation.error};
      }
      if (!validation.accepted) {
        return {.error = QStringLiteral(
            "The shared photo grade exceeds delivery interpolation limits at 97³ (p99 %1, max %2).")
                         .arg(validation.p99_absolute_error, 0, 'g', 6)
                         .arg(validation.maximum_absolute_error, 0, 'g', 6),
                .size = built.lut->size,
                .validation_sample_count = validation.sample_count,
                .validation_p99_absolute_error = validation.p99_absolute_error,
                .validation_maximum_absolute_error = validation.maximum_absolute_error};
      }
    }
  }
  QString error;
  if (video_grade_cache_root().isEmpty() ||
      !QDir().mkpath(video_grade_cache_root()) ||
      !write_grade_lut_file(file_path, *built.lut, &error)) {
    return {.error = error.isEmpty()
                         ? QStringLiteral("Cannot create the timeline video grade cache folder.")
                         : error};
  }
  prune_video_grade_lut_disk_cache(file_path);
  const auto selected_size = built.lut->size;
  auto shared_lut = std::shared_ptr<const photo_editor::pipeline::ColorLut3D>(
      std::move(built.lut));
  shared_lut = remember_grade_lut(
      cache_key,
      std::move(shared_lut),
      file_path,
      validation.sample_count,
      validation.p99_absolute_error,
      validation.maximum_absolute_error);
  return {
      .lut = std::move(shared_lut),
      .file_path = file_path,
      .size = selected_size,
      .validation_sample_count = validation.sample_count,
      .validation_p99_absolute_error = validation.p99_absolute_error,
      .validation_maximum_absolute_error = validation.maximum_absolute_error,
  };
}

QVariantMap timeline_video_grade_cache_metrics() {
  auto metrics = QVariantMap{};
  {
    auto& cache = grade_state_cache();
    const std::lock_guard<std::mutex> lock(cache.mutex);
    metrics.insert(QStringLiteral("gradeStateEntries"),
                   static_cast<qulonglong>(cache.entries.size()));
    metrics.insert(QStringLiteral("gradeStateAccountedPayloadBytes"),
                   static_cast<qulonglong>(cache.resident_bytes));
    metrics.insert(QStringLiteral("gradeStatePayloadBudgetBytes"),
                   static_cast<qulonglong>(kGradeStateCacheBudgetBytes));
    metrics.insert(QStringLiteral("gradeStateHits"),
                   static_cast<qulonglong>(cache.hits));
    metrics.insert(QStringLiteral("gradeStateMisses"),
                   static_cast<qulonglong>(cache.misses));
    metrics.insert(QStringLiteral("gradeStateEvictions"),
                   static_cast<qulonglong>(cache.evictions));
  }
  {
    auto& cache = grade_lut_cache();
    const std::lock_guard<std::mutex> lock(cache.mutex);
    metrics.insert(QStringLiteral("gradeLutEntries"),
                   static_cast<qulonglong>(cache.entries.size()));
    metrics.insert(QStringLiteral("gradeLutTableBytes"),
                   static_cast<qulonglong>(cache.resident_table_bytes));
    metrics.insert(QStringLiteral("gradeLutEntryLimit"),
                   static_cast<qulonglong>(kVideoGradeLutCacheEntries));
    metrics.insert(QStringLiteral("gradeLutTableBudgetBytes"),
                   static_cast<qulonglong>(kVideoGradeLutCacheBudgetBytes));
    metrics.insert(QStringLiteral("gradeLutHits"),
                   static_cast<qulonglong>(cache.hits));
    metrics.insert(QStringLiteral("gradeLutMisses"),
                   static_cast<qulonglong>(cache.misses));
    metrics.insert(QStringLiteral("gradeLutEvictions"),
                   static_cast<qulonglong>(cache.evictions));
  }
  return metrics;
}

}  // namespace photo_editor::desktop

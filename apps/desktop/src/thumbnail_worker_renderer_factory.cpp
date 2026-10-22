#include "photo_paint_surface.h"
#include "thumbnail_worker_renderer_factory.h"

#include "canonical_composite_mask_alpha.h"
#include "edit_recipe_pipeline_bridge.h"
#include "desktop_pipeline_state_compiler.h"
#include "edit_recipe_mask_schema.h"
#include "local_mask_set_settings.h"
#include "photo_editor/pipeline/local_mask_sampling.h"
#include "mask_alpha_provider.h"
#include "mask_thumbnail_payload_service.h"
#include "mask_presentation_schema.h"
#include "mask_range_preview_math.h"
#include "mask_source_revision.h"
#include "photo_render_utils.h"
#include "photo_layer_adjustment_recipe.h"
#include "scaled_mask_matte_service.h"
#include "thumbnail_render_job.h"
#include "working_image.h"

#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QPainter>
#include <QPen>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <utility>
namespace photo_editor::desktop {
namespace {

QString render_source_layer_thumbnail(
    const QVariantMap& layer,
    const ThumbnailRefreshQueueRequest& request,
    QVariantMap* metadata) {
  const photo_editor::pipeline::CancellationToken cancellation{
      .cancellation_requested = request.cancellation.get()};
  if (cancellation.cancelled()) return {};

  // Content thumbnail, not a miniature composite: a fixed neutral develop
  // isolates the source's camera metadata from the document's RAW settings,
  // masks, grade and output profile. Always publish a tagged sRGB PNG.
  const EditRecipeState neutral;
  photo_editor::pipeline::DecodedImageMetadata source_metadata;
  QImage decoded;
  const auto embedded = layer.value(QStringLiteral("pixelData")).toByteArray();
  const auto path = layer.value(QStringLiteral("sourcePath")).toString();
  if (layer.contains("paintTiles")) {
    const auto canvas = QSize(layer.value("pixelWidth").toInt(),layer.value("pixelHeight").toInt());
    decoded = rasterize_photo_paint_layer(layer,canvas.scaled(128,128,Qt::KeepAspectRatio));
  } else if (!embedded.isEmpty()) {
    decoded = QImage::fromData(embedded, "PNG");
  } else if (!decode_source_image(
                 path,
                 &decoded, 128, neutral, &source_metadata, cancellation)) {
    return {};
  }
  if (decoded.isNull() || cancellation.cancelled()) return {};
  const auto scene_linear_source = embedded.isEmpty() &&
      (source_metadata.is_raw || path.endsWith(QStringLiteral(".exr"), Qt::CaseInsensitive));
  if (!scene_linear_source && !decoded.colorSpace().isValid()) {
    decoded.setColorSpace(QColorSpace(QColorSpace::SRgb));
  }
  auto content_state = neutral;
  if (layer.value(QStringLiteral("hasNonNeutralAdjustments")).toBool()) {
    auto recipe = photo_layer_adjustment_recipe(layer);
    recipe.insert(QStringLiteral("kind"), QStringLiteral("adjustment"));
    recipe.insert(QStringLiteral("id"), QStringLiteral("thumbnail-content-colors"));
    content_state.layers = {recipe};
    content_state.photographic_rendering_model = request.state.photographic_rendering_model;
  }
  const auto rendered = scene_linear_source || !content_state.layers.isEmpty() ? render_output_image_with_backend(
      decoded, content_state, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly, &source_metadata, nullptr, {},
      photo_editor::pipeline::RemovalQuality::Final,
      RenderOutputPolicy::DisplayReferred, nullptr, cancellation)
      : convert_tagged_qimage_color_space(decoded, QColorSpace(QColorSpace::SRgb),
            QImage::Format_ARGB32, QImageColorConversionPurpose::OutputPublication);
  if (rendered.isNull() || cancellation.cancelled()) return {};
  auto thumbnail = scaled_layer_thumbnail_image(rendered);
  thumbnail.setColorSpace(QColorSpace(QColorSpace::SRgb));
  const auto source = encoded_layer_thumbnail_source(thumbnail);
  if (source.isEmpty() || cancellation.cancelled()) return {};
  if (metadata != nullptr) {
    const auto content_colors = !content_state.layers.isEmpty();
    metadata->insert(QStringLiteral("thumbnailRenderSource"), content_colors
        ? QStringLiteral("layer-content-colors") : scene_linear_source
        ? QStringLiteral("source-neutral-develop") : QStringLiteral("source-icc-conversion"));
    metadata->insert(QStringLiteral("thumbnailSharedRenderPath"), scene_linear_source || content_colors);
    metadata->insert(QStringLiteral("thumbnailSharedRenderPathContract"),
        content_colors ? QStringLiteral("raster-content-colors-canonical-output-v1")
        : scene_linear_source ? QStringLiteral("source-neutral-canonical-output-v1")
                            : QStringLiteral("source-display-referred-icc-v1"));
    metadata->insert(QStringLiteral("thumbnailColorManaged"), true);
    metadata->insert(QStringLiteral("thumbnailColorManagementContract"),
        QStringLiteral("source-thumbnail-v1|own-metadata|raw:neutral-aces2|display:icc|srgb|document-grade:none"));
    metadata->insert(QStringLiteral("thumbnailPreviewRenderMode"), QStringLiteral("source_neutral_srgb"));
    metadata->insert(QStringLiteral("thumbnailSourceWorkingProfile"), scene_linear_source
        ? neutral.working_color_profile : QStringLiteral("Embedded ICC"));
    metadata->insert(QStringLiteral("thumbnailSourceRenderingModel"), scene_linear_source
        ? neutral.photographic_rendering_model : QStringLiteral("none-display-referred"));
  }
  return source;
}

QVariantList thumbnail_mask_maps_for_selection_layer(
    const QVariantList& mask_stack,
    const QVariantMap& layer,
    const QSize& thumbnail_size) {
  QVariantList masks;
  if (!layer.value(QStringLiteral("selectionScoped")).toBool()) {
    return masks;
  }
  const auto mask_set_ids = selection_mask_set_ids_for_layer(layer);
  const auto mask_set_names = selection_mask_set_names_for_layer(layer);
  if (mask_set_ids.isEmpty() && mask_set_names.isEmpty()) {
    return masks;
  }

  masks.reserve(mask_stack.size());
  QSet<QString> appended_source_ids;
  const auto append_matching_masks = [&](const QString& target_id,
                                         const QString& target_name) {
    for (const auto& value : mask_stack) {
      const auto mask = value.toMap();
      if (!is_selection_mask(mask)) {
        continue;
      }
      const auto set_id =
          mask.value(QStringLiteral("maskSetId")).toString().simplified();
      const auto set_name =
          mask.value(QStringLiteral("maskSetName")).toString().simplified();
      const auto matches =
          (!target_id.isEmpty() && set_id == target_id) ||
          (target_id.isEmpty() && !target_name.isEmpty() &&
           set_name == target_name);
      if (!matches) {
        continue;
      }
      const auto source_id =
          mask.value(QStringLiteral("id")).toString().simplified();
      if (!source_id.isEmpty() && appended_source_ids.contains(source_id)) {
        continue;
      }
      if (!source_id.isEmpty()) {
        appended_source_ids.insert(source_id);
      }
      masks.append(mask_with_scaled_segmentation_matte(
          mask,
          thumbnail_size,
          QStringLiteral("layer-thumbnail")));
    }
  };
  const auto ordered_count = std::max(mask_set_ids.size(), mask_set_names.size());
  for (auto index = 0; index < ordered_count; ++index) {
    append_matching_masks(
        index < mask_set_ids.size() ? mask_set_ids.at(index) : QString(),
        index < mask_set_names.size() ? mask_set_names.at(index) : QString());
  }
  return masks;
}

QImage adjustment_layer_effect_preview_image(
    const QImage& before,
    const QImage& after) {
  if (before.isNull() || after.isNull()) {
    return {};
  }
  const auto before_thumbnail = scaled_layer_thumbnail_image(before);
  const auto after_thumbnail = scaled_layer_thumbnail_image(after);
  if (before_thumbnail.isNull() || after_thumbnail.isNull() ||
      before_thumbnail.size() != after_thumbnail.size()) {
    return {};
  }

  QImage split(before_thumbnail.size(), QImage::Format_ARGB32_Premultiplied);
  split.fill(Qt::transparent);
  QPainter painter(&split);
  const auto split_x = split.width() / 2;
  painter.drawImage(
      QRect(0, 0, split_x, split.height()),
      before_thumbnail,
      QRect(0, 0, split_x, split.height()));
  painter.drawImage(
      QRect(split_x, 0, split.width() - split_x, split.height()),
      after_thumbnail,
      QRect(split_x, 0, split.width() - split_x, split.height()));
  painter.setPen(QPen(QColor::fromRgb(12, 244, 204), 2));
  painter.drawLine(split_x, 0, split_x, split.height());
  return split;
}

void insert_adjustment_layer_effect_preview_fields(
    QVariantMap* metadata,
    const QImage& before,
    const QImage& after) {
  if (metadata == nullptr) {
    return;
  }
  const auto effect_preview =
      adjustment_layer_effect_preview_image(before, after);
  const auto effect_source = encoded_layer_thumbnail_source(effect_preview);
  if (effect_source.isEmpty()) {
    return;
  }
  metadata->insert(QStringLiteral("thumbnailEffectPreviewSource"), effect_source);
  metadata->insert(
      QStringLiteral("thumbnailEffectPreviewContract"),
      QStringLiteral("adjustment-layer-before-after-thumbnail-v1"));
  metadata->insert(
      QStringLiteral("thumbnailEffectPreviewKind"),
      QStringLiteral("before-after-split"));
  metadata->insert(
      QStringLiteral("thumbnailEffectPreviewSplitX"),
      effect_preview.width() / 2);
  metadata->insert(QStringLiteral("thumbnailDisplaySource"), effect_source);
  metadata->insert(
      QStringLiteral("thumbnailDisplayKind"),
      QStringLiteral("effect-preview"));
  metadata->insert(
      QStringLiteral("thumbnailEffectPreviewSignature"),
      QStringLiteral("effect-preview:before-after-split|size:%1x%2|split:%3")
          .arg(effect_preview.width())
          .arg(effect_preview.height())
          .arg(effect_preview.width() / 2));
}

void insert_adjustment_render_metadata(
    QVariantMap* metadata,
    const QImage& base_thumbnail,
    const QImage& rendered_thumbnail,
    const QVariantMap& alpha_metadata,
    const QVariantMap& trace_metadata,
    const QVariantMap& layer,
    const QString& mask_set_id,
    const QString& recipe_stage,
    qint64 recipe_elapsed_ms,
    const QString& composite_stage,
    qint64 composite_elapsed_ms,
    const QString& render_source,
    bool preview_cache_hit,
    const QString& render_mode,
    const QString& backend,
    const QString& publish_path,
    qint64 mask_alpha_elapsed_ms,
    qint64 total_elapsed_ms) {
  if (metadata == nullptr) {
    return;
  }
  metadata->insert(
      QStringLiteral("thumbnail_render_stage_contract"),
      QStringLiteral("layer-mask-render-stage-telemetry-v1"));
  metadata->insert(QStringLiteral("thumbnail_layer_recipe_stage"), recipe_stage);
  metadata->insert(QStringLiteral("thumbnail_layer_recipe_evaluation_ms"), recipe_elapsed_ms);
  metadata->insert(
      QStringLiteral("thumbnail_mask_alpha_stage"),
      QStringLiteral("mask_alpha_retrieval"));
  metadata->insert(QStringLiteral("thumbnail_mask_alpha_retrieval_ms"), mask_alpha_elapsed_ms);
  metadata->insert(QStringLiteral("thumbnail_layer_composite_stage"), composite_stage);
  metadata->insert(QStringLiteral("thumbnail_layer_composite_ms"), composite_elapsed_ms);
  metadata->insert(QStringLiteral("thumbnail_render_stage"), QStringLiteral("thumbnail_render"));
  metadata->insert(QStringLiteral("thumbnail_render_elapsed_ms"), total_elapsed_ms);
  metadata->insert(QStringLiteral("thumbnailRenderSource"), render_source);
  metadata->insert(QStringLiteral("thumbnailSharedRenderPath"), true);
  metadata->insert(
      QStringLiteral("thumbnailSharedRenderPathContract"),
      QStringLiteral("layer-thumbnail-shared-preview-render-v1"));
  metadata->insert(QStringLiteral("thumbnailSharedPreviewCacheHit"), preview_cache_hit);
  metadata->insert(QStringLiteral("thumbnailColorManaged"), true);
  metadata->insert(
      QStringLiteral("thumbnailColorManagementContract"),
      QStringLiteral("layer-thumbnail-active-color-management:v2|adjustment:shared-preview-render|render-mode:layer_thumbnail|fallback:fail-closed"));
  metadata->insert(QStringLiteral("thumbnailPreviewRenderMode"), render_mode);
  metadata->insert(QStringLiteral("thumbnailPreviewRenderBackend"), backend);
  metadata->insert(QStringLiteral("thumbnailPreviewRenderPublishPath"), publish_path);
  metadata->insert(QStringLiteral("thumbnailMaskSetId"), mask_set_id);
  metadata->insert(
      QStringLiteral("thumbnailMasked"),
      layer.value(QStringLiteral("selectionScoped")).toBool());
  insert_adjustment_layer_effect_preview_fields(
      metadata,
      base_thumbnail,
      rendered_thumbnail);
  insert_thumbnail_pixel_metrics(
      metadata,
      rendered_thumbnail.size(),
      QSize(kLayerThumbnailDisplayEdge, kLayerThumbnailDisplayEdge));
  for (auto iterator = alpha_metadata.constBegin(); iterator != alpha_metadata.constEnd();
       ++iterator) {
    metadata->insert(iterator.key(), iterator.value());
  }
  for (auto iterator = trace_metadata.constBegin(); iterator != trace_metadata.constEnd();
       ++iterator) {
    metadata->insert(iterator.key(), iterator.value());
  }
}

QString render_adjustment_layer_thumbnail_source(
    const QImage& source_image,
    const QVariantMap& layer,
    const ThumbnailRefreshQueueRequest& request,
    const QString& source_revision,
    const ThumbnailAdjustmentRendererDependencies& dependencies,
    QVariantMap* metadata) {
  if (metadata != nullptr) {
    metadata->clear();
  }
  if (source_image.isNull() ||
      layer.value(QStringLiteral("kind")).toString().trimmed().toLower() !=
          QStringLiteral("adjustment") ||
      !dependencies.shared_render) {
    return {};
  }

  QElapsedTimer total_timer;
  total_timer.start();
  const auto base_thumbnail = scaled_layer_thumbnail_image(source_image);
  if (base_thumbnail.isNull()) {
    return {};
  }

  const auto mask_set_id = selection_mask_set_identity_signature(layer);
  const auto thumbnail_mask_maps = thumbnail_mask_maps_for_selection_layer(
      request.mask_stack,
      layer,
      base_thumbnail.size());
  if (layer.value(QStringLiteral("selectionScoped")).toBool() &&
      thumbnail_mask_maps.isEmpty()) {
    return {};
  }
  const auto exposure_gain = std::pow(
      2.0,
      layer.value(QStringLiteral("exposure")).toDouble());
  const auto white_balance = preview_white_balance_multipliers(
      layer.value(QStringLiteral("temperature")).toDouble(),
      layer.value(QStringLiteral("tint")).toDouble());
  QElapsedTimer alpha_timer;
  alpha_timer.start();
  const auto alpha_metadata =
      layer.value(QStringLiteral("selectionScoped")).toBool()
          ? canonical_composite_mask_thumbnail_alpha_metadata(
                base_thumbnail,
                thumbnail_mask_maps,
                exposure_gain,
                white_balance)
          : QVariantMap{};
  const auto alpha_elapsed_ms = alpha_timer.elapsed();
  const auto trace_metadata =
      layer.value(QStringLiteral("selectionScoped")).toBool()
          ? composite_mask_alpha_trace_fields(
                QStringLiteral("layer-thumbnail"),
                thumbnail_mask_maps,
                mask_set_id,
                source_revision,
                base_thumbnail.size(),
                request.job.state_revision)
          : QVariantMap{};

  const auto published_preview_eligible =
      request.photo_layers.size() == 1 && !request.mask_thumbnail_dirty &&
      request.published_preview_state_signature == request.job.state_signature;
  if (published_preview_eligible && !request.published_preview_image.isNull()) {
    const auto thumbnail =
        scaled_layer_thumbnail_image(request.published_preview_image);
    if (!thumbnail.isNull()) {
      insert_adjustment_render_metadata(
          metadata,
          base_thumbnail,
          thumbnail,
          alpha_metadata,
          trace_metadata,
          layer,
          mask_set_id,
          QStringLiteral("published_preview_cache"),
          0,
          QStringLiteral("published_preview_cache"),
          0,
          QStringLiteral("shared-preview-render-request"),
          true,
          QStringLiteral("layer_thumbnail"),
          QStringLiteral("published-preview-cache"),
          QStringLiteral("in-memory-published-preview"),
          alpha_elapsed_ms,
          total_timer.elapsed());
      return encoded_layer_thumbnail_source(thumbnail);
    }
  }

  auto thumbnail_state = request.state;
  thumbnail_state.layers = QVariantList{layer};
  thumbnail_state.mask_stack = request.mask_stack;
  thumbnail_state.active_mask_index = -1;
  const auto render_request = ThumbnailAdjustmentSharedRenderRequest{
      .asset_id = request.job.asset_id,
      .file_path = request.job.file_path,
      .state = thumbnail_state,
      .job = make_thumbnail_adjustment_render_job(
          request.job.asset_id,
          request.job.file_path,
          thumbnail_state,
          request.job.state_revision),
      .mask_alpha_trace_fields = trace_metadata,
      .render_reason = QStringLiteral("photo_layer_thumbnail"),
      .interaction_mode = QStringLiteral("thumbnail"),
      .render_mode = QStringLiteral("layer_thumbnail"),
      .render_priority = 4,
      .dirty_domain = QStringLiteral("layers_composite"),
      .dirty_stages = QStringList{
          QStringLiteral("layers_composite"),
          QStringLiteral("lut_post_color"),
          QStringLiteral("display_transform"),
      },
      .max_edge = kLayerThumbnailEdge,
      .jpeg_quality = 90,
      .render_latency_budget_ms = 100,
  };
  const auto shared_render = dependencies.shared_render(render_request);
  if (!shared_render.success || shared_render.image.isNull()) {
    return {};
  }
  const auto thumbnail = scaled_layer_thumbnail_image(shared_render.image);
  if (thumbnail.isNull()) {
    return {};
  }
  insert_adjustment_render_metadata(
      metadata,
      base_thumbnail,
      thumbnail,
      alpha_metadata,
      trace_metadata,
      layer,
      mask_set_id,
      QStringLiteral("shared_preview_render_request"),
      shared_render.adjustment_pipeline_elapsed_ms,
      QStringLiteral("preview_layers_composite"),
      shared_render.adjustment_pipeline_elapsed_ms,
      QStringLiteral("shared-preview-render-request"),
      false,
      shared_render.render_mode,
      shared_render.backend,
      shared_render.publish_path,
      alpha_elapsed_ms,
      total_timer.elapsed());
  return encoded_layer_thumbnail_source(thumbnail);
}

QVariantMap render_mask_thumbnail_entries(
    const ThumbnailRefreshQueueRequest& request) {
  QVariantMap entries;
  if (request.mask_sets.isEmpty()) {
    return entries;
  }

  QImage source_image;
  photo_editor::pipeline::DecodedImageMetadata source_metadata;
  if (!decode_source_image(
          request.job.file_path,
          &source_image,
          kMaskThumbnailBackingEdge,
          request.state,
          &source_metadata,
          photo_editor::pipeline::CancellationToken{
              .cancellation_requested = request.cancellation.get(),
          })) {
    return entries;
  }
  // This image supplies raster dimensions only, never display RGB mask samples.
  const auto thumbnail_image = apply_crop(source_image, request.state);
  if (thumbnail_image.isNull()) {
    return entries;
  }

  const auto pipeline_state = compile_base_color_adjustment_state(
      request.state, &source_metadata);
  const auto& color_pipeline = pipeline_state.color_pipeline;
  const auto preserve_device_rgb =
      photo_editor::pipeline::decoded_image_requires_engine_input_transform(source_metadata);
  // Linearize before crop, exactly as the preview decoder does. Converting a
  // JPEG through a different intermediate QImage format changes ICC rounding.
  const auto full_source = working_image_from_qimage(
      source_image, request.state.working_color_profile,
      &color_pipeline.input_icc_profile_asset, preserve_device_rgb);
  if (full_source.isNull()) return entries;
  auto working = crop_working_image(
      full_source, crop_rect_for_image(source_image.size(), request.state));
  if (working.isNull()) return entries;
  // Match the source-scoped RAW highlight analysis used by cropped previews.
  auto prefix = photo_editor::pipeline::prepare_color_prefix_stage_context(
      full_source.pixels, full_source.width, full_source.height, pipeline_state);
  const photo_editor::pipeline::CancellationToken cancellation{
      .cancellation_requested = request.cancellation.get()};
  using namespace photo_editor::pipeline;
  if (!apply_color_normalize_raw_stage(&working.pixels, working.width, working.height,
          pipeline_state, &prefix, cancellation) ||
      !apply_color_camera_characterization_stage(&working.pixels, working.width, working.height,
          pipeline_state, prefix, cancellation) ||
      !apply_color_scene_adjustments_stage(&working.pixels, working.width, working.height,
          pipeline_state, cancellation) ||
      !apply_color_tone_stage(&working.pixels, working.width, working.height,
          pipeline_state, cancellation) ||
      !apply_color_curves_stage(&working.pixels, working.width, working.height,
          pipeline_state, cancellation) ||
      !apply_color_look_stage(&working.pixels, working.width, working.height,
          pipeline_state, cancellation)) return entries;
  constexpr auto exposure_gain = 1.0;
  constexpr std::array<double, 3> white_balance{1.0, 1.0, 1.0};
  const auto look_pixels = working.pixels;
  const auto active_sets = compile_pipeline_local_mask_sets(
      request.state.mask_stack, DesktopRenderPurpose::Preview,
      LocalMaskRasterPolicy::RebuildEditableAtOutputResolution, thumbnail_image.size());
  QHash<QString, const LocalMaskSet*> active_set_lookup;
  for (const auto& set : active_sets) active_set_lookup.insert(QString::fromStdString(set.id), &set);
  QHash<QString, int> set_order;
  QHash<QString, QVariantMap> set_settings;
  int ordinal = 1;
  for (const auto& value : request.state.mask_stack) {
    const auto mask = normalized_mask_definition(value.toMap(), ordinal++);
    const auto id = mask.value(QStringLiteral("maskSetId")).toString();
    if (!set_order.contains(id)) {
      set_order.insert(id, ordinal);
      set_settings.insert(id, mask);
    }
  }
  LocalMaskSet composite_settings;
  bool composite_phase = false;
  const auto thumbnail_source_revision = mask_tile_source_revision(
      request.job.file_path,
      request.state,
      thumbnail_image.size());
  const auto queue_dependencies = ThumbnailRefreshQueueMaskThumbnailDependencies{
      .scaled_mask_source = mask_with_scaled_segmentation_matte,
      .thumbnail_payload = [&working, &look_pixels, &color_pipeline,
                            &composite_settings, &composite_phase, cancellation](
          const QImage& image, const QVariantList& masks, double,
          const std::array<double, 3>&) -> QVariantMap {
        if (cancellation.cancelled() || masks.isEmpty()) return {};
        auto mask_set = composite_phase ? composite_settings : LocalMaskSet{};
        mask_set.sources.clear();
        for (const auto& value : masks) {
          mask_set.sources.push_back(compile_pipeline_local_mask(
              value.toMap(), LocalMaskRasterPolicy::RebuildEditableAtOutputResolution,
              image.size()));
        }
        const auto alpha = rasterize_local_mask_set_alpha_with_context(
            mask_set, working.width, working.height, working.pixels, look_pixels, color_pipeline);
        if (alpha.size() != working.pixels.size() || cancellation.cancelled()) return {};
        const auto evaluated = evaluate_mask_alpha(
            image.size(), static_cast<int>(masks.size()),
            [&alpha, &working](int x, int y, double, double) {
              return static_cast<double>(alpha[static_cast<std::size_t>(
                  y * working.width + x)]) / 255.0;
            });
        // Evaluate in the photo's aspect ratio before making the square UI
        // thumbnail, so the source samples and spatial mask coordinates agree.
        const auto display_alpha = scaled_square_thumbnail_image(
            grayscale_mask_alpha_image(evaluated), kMaskThumbnailBackingEdge);
        const auto display_cache_key = QStringLiteral("sha1:%1").arg(QString::fromLatin1(
            QCryptographicHash::hash(
                QStringLiteral("mask-input-v3|%1|%2x%3|%4|square:%5")
                    .arg(evaluated.alpha_hash).arg(working.width).arg(working.height)
                    .arg(masks.size()).arg(kMaskThumbnailBackingEdge).toUtf8(),
                QCryptographicHash::Sha1).toHex()));
        const auto displayed = evaluate_mask_alpha_cached(
            display_cache_key,
            display_alpha.size(), static_cast<int>(masks.size()),
            [&display_alpha](int x, int y, double, double) {
              return static_cast<double>(qAlpha(display_alpha.pixel(x, y))) / 255.0;
            });
        auto payload = mask_thumbnail_payload_from_alpha(displayed);
        payload.insert(QStringLiteral("thumbnailMaskInputContract"),
            QStringLiteral("mask-input-v3|float|source-metadata|working-profile|through-look|stack-prefix|fixed-edge-reference"));
        return payload;
      },
      .thumbnail_metadata = scaled_mask_matte_metadata,
      .insert_source_alpha_trace_fields = insert_mask_alpha_trace_fields,
      .composite_alpha_trace_fields = composite_mask_alpha_trace_fields,
  };

  auto ordered_sets = request.mask_sets;
  std::stable_sort(ordered_sets.begin(), ordered_sets.end(), [&set_order](const auto& left, const auto& right) {
    return set_order.value(left.toMap().value(QStringLiteral("id")).toString(), -1) <
        set_order.value(right.toMap().value(QStringLiteral("id")).toString(), -1);
  });
  std::size_t next_active_set = 0;
  auto preceding = pipeline_state;
  preceding.local_mask_sets.clear();
  preceding.local_masks.clear();
  preceding.retouch_masks.clear();
  preceding.photo_layers.clear();
  preceding.linear_mask_enabled = false;
  preceding.radial_mask_enabled = false;
  for (const auto& set_value : ordered_sets) {
    if (thumbnail_refresh_cancelled(request)) {
      break;
    }
    const auto set = set_value.toMap();
    const auto sources = set.value(QStringLiteral("sources")).toList();
    const auto set_id = set.value(QStringLiteral("id")).toString();
    composite_settings = compile_local_mask_set_settings(set_settings.value(
        set_id, sources.isEmpty() ? QVariantMap{} : sources.first().toMap()));
    if (const auto* active = active_set_lookup.value(set_id, nullptr)) composite_settings = *active;
    // Advance once in recipe order while retaining the immutable edge image.
    // This avoids replaying every preceding adjustment for every UI row.
    while (next_active_set < active_sets.size() &&
        set_order.value(QString::fromStdString(active_sets[next_active_set].id), -1) <
            set_order.value(set_id, -1)) {
      preceding.local_mask_sets = {active_sets[next_active_set++]};
      if (!apply_local_adjustments_to_look_pixels_with_context(
              &working.pixels, working.width, working.height, preceding, look_pixels, cancellation)) return {};
    }
    composite_phase = false;
    QVariantList thumbnail_masks;
    for (const auto& source_value : sources) {
      if (thumbnail_refresh_cancelled(request)) {
        break;
      }
      const auto source = source_value.toMap();
      thumbnail_masks.append(mask_with_scaled_segmentation_matte(
          source,
          thumbnail_image.size(),
          QStringLiteral("thumbnail")));
      const auto entry = rendered_mask_source_thumbnail_entry(
          source,
          thumbnail_image,
          thumbnail_source_revision,
          exposure_gain,
          white_balance,
          request,
          queue_dependencies);
      const auto cache_key = entry.value(QStringLiteral("thumbnailCacheKey")).toString();
      if (!cache_key.isEmpty()) {
        entries.insert(cache_key, entry);
      }
    }
    composite_phase = true;
    const auto set_entry = rendered_mask_set_thumbnail_entry(
        set,
        thumbnail_masks,
        thumbnail_image,
        thumbnail_source_revision,
        exposure_gain,
        white_balance,
        request,
        queue_dependencies);
    const auto set_cache_key =
        set_entry.value(QStringLiteral("thumbnailCacheKey")).toString();
    if (!set_cache_key.isEmpty()) {
      entries.insert(set_cache_key, set_entry);
    }
  }
  return entries;
}

}  // namespace

ThumbnailRefreshQueueRenderers make_thumbnail_worker_renderers(
    ThumbnailAdjustmentRendererDependencies adjustment_renderer_dependencies) {
  return ThumbnailRefreshQueueRenderers{
      .adjustment_layer_dependencies =
          ThumbnailRefreshQueueAdjustmentLayerDependencies{
              .recipe_hash = [](const QVariantMap& layer) {
                return photo_layer_thumbnail_recipe_hash(layer);
              },
              .entry_recipe_hash = [](const QVariantMap& layer) {
                return photo_layer_thumbnail_entry_recipe_hash(layer);
              },
              .thumbnail_source =
                  [renderer_dependencies =
                       std::move(adjustment_renderer_dependencies)](
                      const QVariantMap& layer,
                      const QImage& cropped_image,
                      const ThumbnailRefreshQueueRequest& request,
                      QVariantMap* metadata) {
                    return render_adjustment_layer_thumbnail_source(
                        cropped_image,
                        layer,
                        request,
                        mask_tile_source_revision(
                            request.job.file_path,
                            request.state,
                            QSize(kLayerThumbnailEdge, kLayerThumbnailEdge)),
                        renderer_dependencies,
                        metadata);
                  },
          },
      .adjustment_thumbnail_source_image =
          [](const ThumbnailRefreshQueueRequest& request) {
            QImage source_image;
            if (!decode_source_image(
                    request.job.file_path,
                    &source_image,
                    96,
                    request.state,
                    nullptr,
                    photo_editor::pipeline::CancellationToken{
                        .cancellation_requested = request.cancellation.get(),
                    })) {
              return QImage{};
            }
            return apply_crop(source_image, request.state);
          },
      .mask_thumbnail_entries = render_mask_thumbnail_entries,
      .source_layer_thumbnail = render_source_layer_thumbnail,
  };
}

}  // namespace photo_editor::desktop

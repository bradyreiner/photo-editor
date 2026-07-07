#pragma once

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QSize>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace photo_editor::desktop {

inline constexpr auto kSharedMaskAlphaProviderContract = "shared-mask-alpha-provider-v1";

struct MaskAlphaEvaluationResult final {
  QByteArray alpha_mask;
  QSize size;
  QString alpha_hash;
  QString cache_key;
  int source_count = 0;
  bool cache_hit = false;
  bool valid = false;
};

using MaskAlphaWeightEvaluator =
    std::function<double(double normalized_x, double normalized_y, const QColor& rgb)>;
using MaskAlphaSpatialWeightEvaluator =
    std::function<double(int x, int y, double normalized_x, double normalized_y)>;

QString shared_mask_alpha_provider_contract();

QString mask_alpha_source_revision_hash(const QString& source_revision);

MaskAlphaEvaluationResult evaluate_mask_alpha(
    const QImage& source_image,
    int source_count,
    const MaskAlphaWeightEvaluator& evaluator);

MaskAlphaEvaluationResult evaluate_mask_alpha_cached(
    const QString& cache_key,
    const QImage& source_image,
    int source_count,
    const MaskAlphaWeightEvaluator& evaluator);

MaskAlphaEvaluationResult evaluate_mask_alpha(
    const QSize& target_size,
    int source_count,
    const MaskAlphaSpatialWeightEvaluator& evaluator);

MaskAlphaEvaluationResult evaluate_mask_alpha_cached(
    const QString& cache_key,
    const QSize& target_size,
    int source_count,
    const MaskAlphaSpatialWeightEvaluator& evaluator);

QVariantMap mask_alpha_thumbnail_metadata(const MaskAlphaEvaluationResult& result);

QImage grayscale_mask_alpha_image(const MaskAlphaEvaluationResult& result);

QVariantMap shared_mask_alpha_provider_fields(
    const QString& provider,
    bool available,
    const QSize& target_size,
    const QString& aggregate_label = {},
    int aggregate_count = 0);

QVariantMap compose_composite_mask_alpha_trace_fields(
    const QString& trace_context,
    const QString& mask_set_id,
    const QVariantList& component_signatures,
    const QVariantList& component_ids,
    const QVariantList& component_hashes,
    const QVariantList& component_providers,
    const QString& source_revision_hash,
    const QSize& raster_size,
    int mask_work_state_revision);

QVariantMap compose_layer_composite_mask_alpha_trace_fields(
    const QVariantList& layer_signatures,
    const QVariantList& layer_ids,
    const QVariantList& layer_mask_set_ids,
    const QVariantList& layer_hashes,
    const QVariantList& layer_providers,
    const QSize& raster_size,
    int mask_work_state_revision);

QVariantMap compose_outline_composite_mask_alpha_trace_fields(
    const QString& active_source_id,
    const QString& active_set_id,
    const QVariantList& component_signatures,
    const QVariantList& component_ids,
    const QVariantList& component_hashes,
    const QVariantList& component_providers,
    const QSize& raster_size,
    int mask_work_state_revision);

}  // namespace photo_editor::desktop

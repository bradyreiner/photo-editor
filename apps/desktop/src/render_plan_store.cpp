#include "render_plan_store.h"

#include "decoder_provenance.h"
#include "openexr_source_color.h"
#include "edit_recipe_pipeline_bridge.h"
#include "edit_recipe_v13_adapter.h"
#include "linked_image_source.h"
#include "raw_decode_policy.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QStringList>
#include <QWaitCondition>
namespace photo_editor::desktop {
namespace {

constexpr auto kCanonicalPlanAdmissionCacheCapacity = 64;

QString admission_cache_key(
    const QString& recipe_digest,
    const QString& source_asset_id,
    const QString& decoder_provenance,
    bool raw_source,
    const QSize& source_size,
    photo_editor::pipeline::RenderPurpose purpose,
    photo_editor::pipeline::DisplayRenderingTarget rendering_target,
    const std::optional<photo_editor::pipeline::AssetIdentifier>& device_icc_profile,
    photo_editor::pipeline::RenderScopeDomain scope_domain) {
  const auto payload = QStringLiteral(
      "canonical-plan-admission-cache:v1|recipe:%1|source:%2|decoder:%3|raw:%4|size:%5x%6|purpose:%7|target:%8|device:%9|scope:%10")
                           .arg(recipe_digest, source_asset_id, decoder_provenance)
                           .arg(raw_source ? 1 : 0)
                           .arg(source_size.width())
                           .arg(source_size.height())
                           .arg(static_cast<int>(purpose))
                           .arg(static_cast<int>(rendering_target))
                           .arg(device_icc_profile.has_value()
                                    ? QString::fromStdString(device_icc_profile->value)
                                    : QStringLiteral("none"))
                           .arg(static_cast<int>(scope_domain));
  return QString::fromLatin1(QCryptographicHash::hash(
      payload.toUtf8(), QCryptographicHash::Sha256).toHex());
}

}  // namespace

struct RenderPlanStore::Cache final {
  QMutex mutex;
  QWaitCondition ready;
  QHash<QString, CanonicalRenderPlanAdmission> entries;
  QList<QString> lru;
  QSet<QString> building;
  quint64 hit_count = 0;
  quint64 build_count = 0;
};

RenderPlanStore::RenderPlanStore() : cache_(std::make_unique<Cache>()) {}
RenderPlanStore::~RenderPlanStore() = default;

RenderPlanStore& RenderPlanStore::shared() {
  static RenderPlanStore store;
  return store;
}

DecoderProvenanceResult resolve_decoder_provenance_for_source(
    const QString& source_path,
    const EditRecipeState& state,
    const photo_editor::pipeline::DecodedImageMetadata* metadata) {
  DecoderProvenanceResult result;
  auto resolved_metadata = metadata == nullptr
                               ? photo_editor::pipeline::DecodedImageMetadata{}
                               : *metadata;
  result.raw_source = resolved_metadata.valid
                          ? resolved_metadata.is_raw
                          : raw_image_supported(source_path);
  if (result.raw_source && !resolved_metadata.valid) {
    static_cast<void>(read_raw_decoded_metadata(
        source_path, &resolved_metadata, raw_decode_settings_for_state(state)));
  }
  if (result.raw_source &&
      !photo_editor::pipeline::decoded_image_color_boundary_valid(resolved_metadata)) {
    result.error = QStringLiteral(
        "canonical RAW recipe requires a valid decoded color boundary");
    return result;
  }
  if (result.raw_source &&
      photo_editor::pipeline::decoded_image_requires_engine_input_transform(
          resolved_metadata)) {
    const auto resolved_config =
        compile_color_pipeline_config(state, &resolved_metadata);
    if (!resolved_config.camera_profile.valid) {
      result.error = QStringLiteral(
          "canonical RAW recipe requires executable camera characterization: %1")
                         .arg(QString::fromStdString(
                             resolved_config.camera_profile.failure_reason));
      return result;
    }
  }
  if (!result.raw_source && QFileInfo(source_path).suffix().compare(QStringLiteral("exr"), Qt::CaseInsensitive) == 0) {
    result.provenance = QString::fromLatin1(kOpenExrSourceColorContract) + QStringLiteral("|backend:") +
        QString::fromUtf8(photo_editor::io::openexr_backend_version().data(),
            static_cast<qsizetype>(photo_editor::io::openexr_backend_version().size()));
  } else if (resolved_metadata.valid) {
    result.provenance = QStringLiteral("%1|encoding:%2|profile:%3")
                            .arg(
                                QString::fromStdString(
                                    resolved_metadata.decoder_backend),
                                QString::fromLatin1(
                                    photo_editor::pipeline::decoded_pixel_encoding_name(
                                        resolved_metadata.pixel_encoding)),
                                QString::fromStdString(
                                    resolved_metadata.camera_profile_identity));
    result.provenance += QStringLiteral("|white-balance-applied:%1")
                             .arg(resolved_metadata.white_balance_applied ? 1 : 0);
    if (resolved_metadata.decoder_camera_to_xyz_valid) {
      result.provenance += QStringLiteral("|camera-to-xyz:%1")
                               .arg(QString::fromStdString(
                                   resolved_metadata
                                       .decoder_camera_to_xyz_reference_white));
      for (const auto coefficient : resolved_metadata.decoder_camera_to_xyz) {
        result.provenance += QStringLiteral(":%1").arg(coefficient, 0, 'g', 17);
      }
    }
  } else {
    result.provenance = QStringLiteral("qt-raster-linearization-v1|suffix:%1")
                            .arg(QFileInfo(source_path)
                                     .suffix()
                                     .trimmed()
                                     .toLower());
  }
  if (result.provenance.trimmed().isEmpty()) {
    result.error =
        QStringLiteral("canonical recipe requires exact decoder provenance");
  }
  return result;
}

CanonicalRenderPlanAdmission RenderPlanStore::admit(
    const QString& source_asset_id,
    const QString& source_path,
    const EditRecipeState& state,
    const QSize& source_size,
    photo_editor::pipeline::RenderPurpose purpose,
    photo_editor::pipeline::DisplayRenderingTarget rendering_target,
    const std::optional<photo_editor::pipeline::AssetIdentifier>& device_icc_profile,
    const photo_editor::pipeline::DecodedImageMetadata* metadata,
    const QString& recipe_payload_digest,
    photo_editor::pipeline::RenderScopeDomain scope_domain) {
  CanonicalRenderPlanAdmission admission;
  if (const auto error = linked_photo_layer_source_file_error(state.layers);
      !error.isEmpty()) {
    admission.error = error;
    return admission;
  }
  if (source_asset_id.trimmed().isEmpty()) {
    admission.error =
        QStringLiteral("canonical plan requires a stable source asset id");
    return admission;
  }
  if (!source_size.isValid() || source_size.width() <= 0 ||
      source_size.height() <= 0) {
    admission.error =
        QStringLiteral("canonical plan requires non-zero source dimensions");
    return admission;
  }

  auto resolved_metadata = metadata == nullptr
                               ? photo_editor::pipeline::DecodedImageMetadata{}
                               : *metadata;
  const auto source_is_raw = resolved_metadata.valid
                                 ? resolved_metadata.is_raw
                                 : raw_image_supported(source_path);
  if (source_is_raw && !resolved_metadata.valid) {
    static_cast<void>(read_raw_decoded_metadata(
        source_path, &resolved_metadata, raw_decode_settings_for_state(state)));
  }
  const auto provenance = resolve_decoder_provenance_for_source(
      source_path, state, &resolved_metadata);
  if (!provenance.ok()) {
    admission.error = provenance.error;
    return admission;
  }
  const auto raw_source = provenance.raw_source;
  const auto recipe_digest = recipe_payload_digest.trimmed().isEmpty()
                                 ? QString::fromLatin1(QCryptographicHash::hash(
                                       QJsonDocument(build_edit_adjustment_state_object(state)).toJson(QJsonDocument::Compact),
                                       QCryptographicHash::Sha256)
                                       .toHex())
                                 : recipe_payload_digest.trimmed();
  const auto cache_key = admission_cache_key(
      recipe_digest + linked_photo_layer_sources_revision(state.layers),
      source_asset_id.trimmed(),
      provenance.provenance,
      raw_source,
      source_size,
      purpose,
      rendering_target,
      device_icc_profile,
      scope_domain);
  for (;;) {
    QMutexLocker lock(&cache_->mutex);
    const auto cached = cache_->entries.constFind(cache_key);
    if (cached != cache_->entries.cend()) {
      cache_->lru.removeAll(cache_key);
      cache_->lru.push_back(cache_key);
      ++cache_->hit_count;
      admission = cached.value();
      admission.cache_hit = true;
      admission.cache_resident_entries =
          static_cast<int>(cache_->entries.size());
      admission.cache_hit_count = cache_->hit_count;
      admission.cache_build_count = cache_->build_count;
      return admission;
    }
    if (!cache_->building.contains(cache_key)) {
      cache_->building.insert(cache_key);
      ++cache_->build_count;
      break;
    }
    cache_->ready.wait(&cache_->mutex);
  }

  const auto compiled = compile_desktop_render_plan_with_derived_assets(
      state,
      DesktopRenderPlanRequest{
          .recipe_id = QStringLiteral("desktop-v13:%1").arg(recipe_digest),
          .recipe_payload_digest = recipe_digest,
          .source_id = source_asset_id.trimmed(),
          .decoder_provenance = provenance.provenance,
          .raw_source = raw_source,
          .source_width = static_cast<std::uint32_t>(source_size.width()),
          .source_height = static_cast<std::uint32_t>(source_size.height()),
          .rendering_target = rendering_target,
          .device_icc_profile = device_icc_profile,
          .purpose = purpose,
          .scope_domain = scope_domain});
  if (!compiled.ok()) {
    QStringList errors;
    for (const auto& error : compiled.adapter_errors) {
      errors.push_back(QStringLiteral("%1: %2").arg(
          QString::fromStdString(error.path),
          QString::fromStdString(error.message)));
    }
    for (const auto& error : compiled.compiler_errors) {
      errors.push_back(QStringLiteral("%1: %2").arg(
          QString::fromStdString(error.path),
          QString::fromStdString(error.message)));
    }
    admission.error = errors.join(QStringLiteral("; "));
    if (admission.error.isEmpty()) {
      admission.error =
          QStringLiteral("canonical render plan compilation failed");
    }
    QMutexLocker lock(&cache_->mutex);
    cache_->building.remove(cache_key);
    admission.cache_resident_entries =
        static_cast<int>(cache_->entries.size());
    admission.cache_hit_count = cache_->hit_count;
    admission.cache_build_count = cache_->build_count;
    cache_->ready.wakeAll();
    return admission;
  }

  admission.admitted = true;
  admission.plan = compiled.plan;
  admission.plan_cache_key =
      QString::fromStdString(compiled.plan->plan_cache_key);
  admission.recipe_cache_key =
      QString::fromStdString(compiled.plan->recipe_cache_key);
  admission.serialized_plan = QString::fromStdString(
      photo_editor::pipeline::serialize_render_plan(*compiled.plan));
  {
    QMutexLocker lock(&cache_->mutex);
    cache_->entries.insert(cache_key, admission);
    cache_->lru.removeAll(cache_key);
    cache_->lru.push_back(cache_key);
    while (cache_->entries.size() > kCanonicalPlanAdmissionCacheCapacity &&
           !cache_->lru.isEmpty()) {
      cache_->entries.remove(cache_->lru.takeFirst());
    }
    cache_->building.remove(cache_key);
    admission.cache_resident_entries =
        static_cast<int>(cache_->entries.size());
    admission.cache_hit_count = cache_->hit_count;
    admission.cache_build_count = cache_->build_count;
    cache_->entries.insert(cache_key, admission);
    cache_->ready.wakeAll();
  }
  return admission;
}

}  // namespace photo_editor::desktop

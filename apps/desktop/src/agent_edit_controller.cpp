#include "agent_edit_controller.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <future>
#include <thread>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include "agent_edit_recipe_patch.h"
#include "agent_image_validation.h"
#include "agent_local_model.h"
#include "app_bootstrap.h"
#include "catalog_asset_model.h"
#include "edit_recipe_state.h"
#include "export_job_controller.h"
#include "observability.h"
#include "photo_render_utils.h"
#include "working_image.h"

namespace photo_editor::desktop {
namespace {

constexpr int kMaxValidationRevisionIterations = 2;
constexpr int kAgentBatchConcurrencyLimit = 1;
constexpr int kInteractiveAgentRunBudgetMs = 30000;
constexpr int kInteractiveAgentPerExtraAssetBudgetMs = 10000;
constexpr int kAgentProofPreviewMaxEdge = 1600;

using ProofCancellationCallback = std::function<bool()>;

QVariantMap result_map(bool ok, const QStringList& errors = {}, const QStringList& warnings = {}) {
  QVariantMap result;
  result.insert(QStringLiteral("ok"), ok);
  result.insert(QStringLiteral("errors"), errors);
  result.insert(QStringLiteral("warnings"), warnings);
  if (!ok && !errors.isEmpty()) {
    result.insert(QStringLiteral("failureReason"), errors.join(QStringLiteral("; ")));
  }
  return result;
}

void record_failure_reason(QVariantMap* report, const QString& reason) {
  if (report == nullptr || reason.trimmed().isEmpty()) {
    return;
  }
  const auto trimmed = reason.trimmed();
  report->insert(QStringLiteral("failureReason"), trimmed);
  if (report->value(QStringLiteral("errors")).toStringList().isEmpty()) {
    report->insert(QStringLiteral("errors"), QStringList{trimmed});
  }
}

bool run_timed_out(const QElapsedTimer& timer, int timeout_ms) {
  return timeout_ms >= 0 && timer.elapsed() >= timeout_ms;
}

QVariantMap agent_proof_render_source_policy(const QString& stage) {
  return QVariantMap{
      {QStringLiteral("stage"), stage},
      {QStringLiteral("sourceResolutionPolicy"), QStringLiteral("bounded-preview")},
      {QStringLiteral("sourceDecodeMaxEdge"), kAgentProofPreviewMaxEdge},
      {QStringLiteral("usesDecodedImageCache"), true},
      {QStringLiteral("fullResolutionRequired"), false},
  };
}

struct WorkerPlanResult final {
  AgentLocalModelPlanResult plan;
  int ui_event_pump_count = 0;
};

WorkerPlanResult plan_agent_recipe_patch_on_worker(const AgentLocalModelPlanRequest& request) {
  auto future = std::async(
      std::launch::async,
      [request]() {
        return plan_agent_recipe_patch(request);
      });

  WorkerPlanResult result;
  while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
    ++result.ui_event_pump_count;
    if (QCoreApplication::instance() != nullptr) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
  result.plan = future.get();
  return result;
}

QString safe_artifact_id(const QString& value, const QString& fallback) {
  const auto trimmed = value.trimmed();
  QString safe;
  safe.reserve(trimmed.size());
  for (const auto character : trimmed) {
    if (character.isLetterOrNumber() || character == QLatin1Char('-') ||
        character == QLatin1Char('_') || character == QLatin1Char('.')) {
      safe.append(character);
    } else {
      safe.append(QLatin1Char('_'));
    }
  }
  return safe.isEmpty() ? fallback : safe;
}

bool write_json_artifact(const QString& path, const QVariantMap& payload, QStringList* warnings) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (warnings != nullptr) {
      warnings->append(QStringLiteral("Unable to write agent artifact: %1").arg(path));
    }
    return false;
  }
  const auto bytes =
      QJsonDocument(QJsonObject::fromVariantMap(payload)).toJson(QJsonDocument::Indented);
  if (file.write(bytes) != bytes.size()) {
    if (warnings != nullptr) {
      warnings->append(QStringLiteral("Unable to write complete agent artifact: %1").arg(path));
    }
    return false;
  }
  if (!file.commit()) {
    if (warnings != nullptr) {
      warnings->append(QStringLiteral("Unable to commit agent artifact: %1").arg(path));
    }
    return false;
  }
  return true;
}

QString normalized_style_id(const QString& name) {
  auto normalized = name.trimmed().toLower();
  normalized.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
  normalized.replace(QRegularExpression(QStringLiteral("^-+|-+$")), QString());
  normalized = normalized.left(48);
  normalized.replace(QRegularExpression(QStringLiteral("-+$")), QString());
  if (normalized.size() < 2) {
    normalized = QStringLiteral("agent-style");
  }
  return normalized;
}

QVariantMap merged_map(QVariantMap values, const QVariantMap& defaults) {
  for (auto iterator = defaults.cbegin(); iterator != defaults.cend(); ++iterator) {
    if (!values.contains(iterator.key())) {
      values.insert(iterator.key(), iterator.value());
    }
  }
  return values;
}

QVariantMap style_storage_document_from_runtime_style(const QVariantMap& style) {
  QVariantMap document{
      {QStringLiteral("schema_version"),
       style.value(QStringLiteral("schema_version"),
                   style.value(QStringLiteral("schemaVersion"),
                               kCurrentAgentEditStyleSchemaVersion))},
      {QStringLiteral("id"), style.value(QStringLiteral("id"))},
      {QStringLiteral("name"), style.value(QStringLiteral("name"))},
      {QStringLiteral("goal"), style.value(QStringLiteral("goal"))},
      {QStringLiteral("allowed_tools"),
       style.value(QStringLiteral("allowed_tools"),
                   style.value(QStringLiteral("allowedTools")))},
      {QStringLiteral("constraints"), style.value(QStringLiteral("constraints"))},
      {QStringLiteral("validation"), style.value(QStringLiteral("validation"))},
      {QStringLiteral("batch_behavior"),
       style.value(QStringLiteral("batch_behavior"),
                   style.value(QStringLiteral("batchBehavior")))},
  };
  const auto reference_policy =
      style.value(QStringLiteral("reference_policy"),
                  style.value(QStringLiteral("referencePolicy")));
  if (reference_policy.isValid() && !reference_policy.toMap().isEmpty()) {
    document.insert(QStringLiteral("reference_policy"), reference_policy);
  }
  return document;
}

QVariantMap recipe_analysis_summary(const EditRecipeState& state, const QString& recipe_json) {
  return QVariantMap{
      {QStringLiteral("recipeJsonBytes"), recipe_json.toUtf8().size()},
      {QStringLiteral("snapshotCount"), static_cast<int>(state.recipe_snapshots.size())},
      {QStringLiteral("maskCount"), state.mask_stack.size()},
      {QStringLiteral("layerCount"), state.layers.size()},
      {QStringLiteral("linearMaskEnabled"), state.linear_mask_enabled},
      {QStringLiteral("radialMaskEnabled"), state.radial_mask_enabled},
      {QStringLiteral("exposure"), state.exposure},
      {QStringLiteral("contrast"), state.contrast},
      {QStringLiteral("highlights"), state.highlights},
      {QStringLiteral("shadows"), state.shadows},
      {QStringLiteral("temperature"), state.temperature},
      {QStringLiteral("tint"), state.tint},
      {QStringLiteral("vibrance"), state.vibrance},
      {QStringLiteral("saturation"), state.saturation},
      {QStringLiteral("clarity"), state.clarity},
      {QStringLiteral("dehaze"), state.dehaze},
      {QStringLiteral("cropPreset"), state.crop_preset},
      {QStringLiteral("cropZoom"), state.crop_zoom},
  };
}

QVariantList agent_edit_goals(
    const QVariantMap& style,
    const QString& instructions,
    const QVariantMap& recipe_patch = {}) {
  QVariantList goals;
  const auto style_id = style.value(QStringLiteral("id")).toString();
  const auto style_name = style.value(QStringLiteral("name")).toString();
  const auto goal = style.value(QStringLiteral("goal")).toString().trimmed();
  if (!goal.isEmpty()) {
    goals.append(QVariantMap{
        {QStringLiteral("source"), QStringLiteral("style")},
        {QStringLiteral("styleId"), style_id},
        {QStringLiteral("styleName"), style_name},
        {QStringLiteral("goal"), goal},
    });
  }
  const auto user_goal = instructions.trimmed();
  if (!user_goal.isEmpty()) {
    goals.append(QVariantMap{
        {QStringLiteral("source"), QStringLiteral("userInstructions")},
        {QStringLiteral("goal"), user_goal},
    });
  }
  if (!recipe_patch.isEmpty()) {
    goals.append(QVariantMap{
        {QStringLiteral("source"), QStringLiteral("plannedTools")},
        {QStringLiteral("groups"), recipe_patch.keys()},
    });
  }
  return goals;
}

QVariantMap agent_risk_assessment(
    const EditRecipeState& state,
    const QStringList& warnings = {},
    const QVariantMap& recipe_patch = {}) {
  QVariantList risks;
  const auto append_risk = [&](const QString& type, const QString& severity, const QString& detail) {
    risks.append(QVariantMap{
        {QStringLiteral("type"), type},
        {QStringLiteral("severity"), severity},
        {QStringLiteral("detail"), detail},
    });
  };

  if (!warnings.isEmpty()) {
    append_risk(
        QStringLiteral("validationWarnings"),
        QStringLiteral("warning"),
        warnings.join(QStringLiteral("; ")));
  }
  if (state.mask_stack.size() > 0 || state.linear_mask_enabled || state.radial_mask_enabled) {
    append_risk(
        QStringLiteral("existingMasks"),
        QStringLiteral("review"),
        QStringLiteral("Current recipe already contains masks or local adjustments."));
  }
  if (state.layers.size() > 0) {
    append_risk(
        QStringLiteral("existingLayers"),
        QStringLiteral("review"),
        QStringLiteral("Current recipe already contains layers or LUT adjustments."));
  }
  if (state.crop_zoom > 1.0001 || state.crop_rect_width > 0.0 || state.crop_rect_height > 0.0) {
    append_risk(
        QStringLiteral("existingCrop"),
        QStringLiteral("review"),
        QStringLiteral("Current recipe already contains crop or framing changes."));
  }
  if (recipe_patch.contains(QStringLiteral("masks")) ||
      recipe_patch.contains(QStringLiteral("retouch")) ||
      recipe_patch.contains(QStringLiteral("layers"))) {
    append_risk(
        QStringLiteral("localizedOrDestructiveTools"),
        QStringLiteral("review"),
        QStringLiteral("Planned patch touches masks, retouch, layers, or LUT stacks."));
  }

  return QVariantMap{
      {QStringLiteral("riskCount"), risks.size()},
      {QStringLiteral("risks"), risks},
      {QStringLiteral("requiresReview"), !risks.isEmpty()},
  };
}

bool batch_behavior_allows_global_group(const QVariantMap& style, const QString& group) {
  const auto batch_behavior = style.value(QStringLiteral("batchBehavior")).toMap();
  if (group == QStringLiteral("crop") || group == QStringLiteral("optics")) {
    return batch_behavior.value(QStringLiteral("allow_global_crop")).toBool();
  }
  if (group == QStringLiteral("masks")) {
    return batch_behavior.value(QStringLiteral("allow_global_masks")).toBool();
  }
  if (group == QStringLiteral("retouch")) {
    return batch_behavior.value(QStringLiteral("allow_global_retouch")).toBool();
  }
  return true;
}

QStringList selected_set_global_edit_errors(const QVariantMap& patch, const QVariantMap& style) {
  QStringList errors;
  for (auto iterator = patch.cbegin(); iterator != patch.cend(); ++iterator) {
    const auto group = iterator.key();
    if (!batch_behavior_allows_global_group(style, group)) {
      errors.append(
          QStringLiteral("Selected-set drafts cannot apply %1 edits globally unless the style batch_behavior explicitly allows it.")
              .arg(group));
    }
  }
  return errors;
}

QString normalized_cluster_signal(const QVariantMap& asset, const QString& key, const QString& fallback) {
  const auto value = asset.value(key).toString().trimmed();
  return value.isEmpty() ? fallback : value.toLower();
}

QString cluster_label_signal(const QVariantMap& asset, const QString& key, const QString& fallback) {
  const auto value = asset.value(key).toString().trimmed();
  return value.isEmpty() ? fallback : value;
}

QString numeric_bucket(double value, double bucket_size, const QString& fallback) {
  if (!std::isfinite(value) || bucket_size <= 0.0) {
    return fallback;
  }
  const auto bucket = static_cast<int>(std::floor(value / bucket_size));
  return QString::number(bucket);
}

QVariantMap recipe_cluster_signals(const QVariantMap& asset) {
  const auto recipe_json = asset.value(QStringLiteral("recipeJson")).toString();
  const auto validation = validate_edit_recipe_json(recipe_json);
  if (!validation.valid) {
    return QVariantMap{
        {QStringLiteral("recipeAvailable"), false},
        {QStringLiteral("whiteBalanceBucket"), QStringLiteral("unknown-wb")},
        {QStringLiteral("exposureBucket"), QStringLiteral("unknown-exposure")},
      };
  }
  const auto state = parse_edit_recipe_json(recipe_json);
  return QVariantMap{
      {QStringLiteral("recipeAvailable"), true},
      {QStringLiteral("whiteBalanceBucket"), numeric_bucket(state.temperature, 5.0, QStringLiteral("unknown-wb"))},
      {QStringLiteral("exposureBucket"), numeric_bucket(state.exposure, 0.5, QStringLiteral("unknown-exposure"))},
  };
}

QString batch_cluster_key(const QVariantMap& asset) {
  const auto recipe_signals = recipe_cluster_signals(asset);
  return QStringList{
      normalized_cluster_signal(asset, QStringLiteral("captureDate"), QStringLiteral("unknown-date")),
      normalized_cluster_signal(asset, QStringLiteral("cameraLabel"), QStringLiteral("unknown-camera")),
      normalized_cluster_signal(asset, QStringLiteral("lensModel"), QStringLiteral("unknown-lens")),
      normalized_cluster_signal(asset, QStringLiteral("iso"), QStringLiteral("unknown-iso")),
      normalized_cluster_signal(asset, QStringLiteral("shutterSpeed"), QStringLiteral("unknown-shutter")),
      normalized_cluster_signal(asset, QStringLiteral("aperture"), QStringLiteral("unknown-aperture")),
      recipe_signals.value(QStringLiteral("whiteBalanceBucket")).toString(),
      recipe_signals.value(QStringLiteral("exposureBucket")).toString(),
  }.join(QStringLiteral("|"));
}

QVariantList batch_cluster_reports(const QVariantList& assets) {
  QVariantList clusters;
  QHash<QString, int> cluster_index_by_key;

  for (const auto& asset_value : assets) {
    const auto asset = asset_value.toMap();
    const auto key = batch_cluster_key(asset);
    auto cluster_index = cluster_index_by_key.value(key, -1);
    if (cluster_index < 0) {
      cluster_index = static_cast<int>(clusters.size());
      cluster_index_by_key.insert(key, cluster_index);
      const auto cluster_id = QStringLiteral("cluster-%1").arg(cluster_index + 1);
      const auto recipe_signals = recipe_cluster_signals(asset);
      clusters.append(QVariantMap{
          {QStringLiteral("clusterId"), cluster_id},
          {QStringLiteral("clusterKey"), key},
          {QStringLiteral("label"),
           QStringLiteral("%1 / %2 / %3")
               .arg(cluster_label_signal(asset, QStringLiteral("captureDate"), QStringLiteral("Unknown date")),
                    cluster_label_signal(asset, QStringLiteral("cameraLabel"), QStringLiteral("Unknown camera")),
                    cluster_label_signal(asset, QStringLiteral("lensModel"), QStringLiteral("Unknown lens")))},
          {QStringLiteral("assetIds"), QStringList{}},
          {QStringLiteral("assetCount"), 0},
          {QStringLiteral("representativeAssetId"), QString()},
          {QStringLiteral("representativeReason"),
           QStringLiteral("First available asset in the metadata and recipe cluster.")},
          {QStringLiteral("signals"),
           QVariantMap{
               {QStringLiteral("captureDate"), asset.value(QStringLiteral("captureDate"))},
               {QStringLiteral("cameraLabel"), asset.value(QStringLiteral("cameraLabel"))},
               {QStringLiteral("lensModel"), asset.value(QStringLiteral("lensModel"))},
               {QStringLiteral("iso"), asset.value(QStringLiteral("iso"))},
               {QStringLiteral("shutterSpeed"), asset.value(QStringLiteral("shutterSpeed"))},
               {QStringLiteral("aperture"), asset.value(QStringLiteral("aperture"))},
               {QStringLiteral("whiteBalanceBucket"), recipe_signals.value(QStringLiteral("whiteBalanceBucket"))},
               {QStringLiteral("exposureBucket"), recipe_signals.value(QStringLiteral("exposureBucket"))},
           }},
          {QStringLiteral("availableSignals"),
           QStringList{
               QStringLiteral("captureDate"),
               QStringLiteral("cameraLabel"),
               QStringLiteral("lensModel"),
               QStringLiteral("iso"),
               QStringLiteral("shutterSpeed"),
               QStringLiteral("aperture"),
               QStringLiteral("recipeWhiteBalance"),
               QStringLiteral("recipeExposure"),
           }},
          {QStringLiteral("unavailableSignals"),
           QStringList{QStringLiteral("histogram"), QStringLiteral("visualSimilarity")}},
          {QStringLiteral("histogramAvailable"), false},
          {QStringLiteral("visualSimilarityAvailable"), false},
          {QStringLiteral("syncGroups"),
           QStringList{
               QStringLiteral("globalTone"),
               QStringLiteral("color"),
               QStringLiteral("curves"),
               QStringLiteral("detail"),
               QStringLiteral("colorMixer"),
               QStringLiteral("colorGrading"),
           }},
          {QStringLiteral("outlierAssetIds"), QStringList{}},
      });
    }

    auto cluster = clusters.at(cluster_index).toMap();
    auto asset_ids = cluster.value(QStringLiteral("assetIds")).toStringList();
    const auto asset_id = asset.value(QStringLiteral("assetId")).toString();
    asset_ids.append(asset_id);
    cluster.insert(QStringLiteral("assetIds"), asset_ids);
    cluster.insert(QStringLiteral("assetCount"), asset_ids.size());
    if (cluster.value(QStringLiteral("representativeAssetId")).toString().isEmpty()) {
      cluster.insert(QStringLiteral("representativeAssetId"), asset_id);
    }
    clusters[cluster_index] = cluster;
  }

  return clusters;
}

QVariantMap batch_cluster_metadata(const QVariantList& assets) {
  const auto clusters = batch_cluster_reports(assets);
  QVariantMap assignments;
  QStringList representative_asset_ids;
  for (const auto& cluster_value : clusters) {
    const auto cluster = cluster_value.toMap();
    const auto cluster_id = cluster.value(QStringLiteral("clusterId")).toString();
    const auto representative_asset_id =
        cluster.value(QStringLiteral("representativeAssetId")).toString();
    representative_asset_ids.append(representative_asset_id);
    for (const auto& asset_id : cluster.value(QStringLiteral("assetIds")).toStringList()) {
      assignments.insert(
          asset_id,
          QVariantMap{
              {QStringLiteral("clusterId"), cluster_id},
              {QStringLiteral("clusterKey"), cluster.value(QStringLiteral("clusterKey"))},
              {QStringLiteral("clusterRepresentativeAssetId"), representative_asset_id},
              {QStringLiteral("clusterSyncGroups"), cluster.value(QStringLiteral("syncGroups"))},
          });
    }
  }
  return QVariantMap{
      {QStringLiteral("batchClusters"), clusters},
      {QStringLiteral("clusterCount"), clusters.size()},
      {QStringLiteral("representativeAssetIds"), representative_asset_ids},
      {QStringLiteral("assetClusterAssignments"), assignments},
  };
}

QVariantList clusters_with_outlier_reports(
    const QVariantList& clusters,
    const QVariantList& asset_reports) {
  QVariantMap outlier_ids_by_cluster;
  for (const auto& asset_report_value : asset_reports) {
    const auto asset_report = asset_report_value.toMap();
    const auto cluster_id = asset_report.value(QStringLiteral("clusterId")).toString();
    if (cluster_id.isEmpty()) {
      continue;
    }
    if (asset_report.value(QStringLiteral("revisionCount")).toInt() <= 0 &&
        !asset_report.value(QStringLiteral("validationIterationLimitReached")).toBool()) {
      continue;
    }
    auto outlier_ids = outlier_ids_by_cluster.value(cluster_id).toStringList();
    outlier_ids.append(asset_report.value(QStringLiteral("assetId")).toString());
    outlier_ids_by_cluster.insert(cluster_id, outlier_ids);
  }

  QVariantList updated_clusters;
  for (const auto& cluster_value : clusters) {
    auto cluster = cluster_value.toMap();
    const auto cluster_id = cluster.value(QStringLiteral("clusterId")).toString();
    const auto outlier_ids = outlier_ids_by_cluster.value(cluster_id).toStringList();
    cluster.insert(QStringLiteral("outlierAssetIds"), outlier_ids);
    cluster.insert(QStringLiteral("outlierCorrectionCount"), outlier_ids.size());
    cluster.insert(
        QStringLiteral("outlierCorrectionPolicy"),
        QStringLiteral("Validation feedback may revise each asset's cluster patch independently before draft apply."));
    updated_clusters.append(cluster);
  }
  return updated_clusters;
}

QString variant_family_id_from_report(const QVariantMap& report) {
  const auto source_asset_id = report.value(QStringLiteral("sourceAssetId")).toString().trimmed();
  if (!source_asset_id.isEmpty()) {
    return source_asset_id;
  }
  return report.value(QStringLiteral("assetId")).toString().trimmed();
}

QVariantList variant_family_summaries(const QVariantList& asset_reports) {
  QVariantMap family_by_id;
  QStringList family_order;
  for (const auto& report_value : asset_reports) {
    const auto report = report_value.toMap();
    const auto asset_id = report.value(QStringLiteral("assetId")).toString();
    if (asset_id.isEmpty()) {
      continue;
    }
    const auto family_id = variant_family_id_from_report(report);
    if (family_id.isEmpty()) {
      continue;
    }
    if (!family_by_id.contains(family_id)) {
      family_order.append(family_id);
      family_by_id.insert(
          family_id,
          QVariantMap{
              {QStringLiteral("sourceAssetId"), family_id},
              {QStringLiteral("targetAssetIds"), QStringList{}},
              {QStringLiteral("virtualCopyAssetIds"), QStringList{}},
              {QStringLiteral("variantNames"), QStringList{}},
              {QStringLiteral("independentRecipes"), true},
          });
    }
    auto family = family_by_id.value(family_id).toMap();
    auto target_asset_ids = family.value(QStringLiteral("targetAssetIds")).toStringList();
    target_asset_ids.append(asset_id);
    family.insert(QStringLiteral("targetAssetIds"), target_asset_ids);
    family.insert(QStringLiteral("targetCount"), target_asset_ids.size());
    if (report.value(QStringLiteral("virtualCopy")).toBool()) {
      auto virtual_copy_asset_ids = family.value(QStringLiteral("virtualCopyAssetIds")).toStringList();
      virtual_copy_asset_ids.append(asset_id);
      family.insert(QStringLiteral("virtualCopyAssetIds"), virtual_copy_asset_ids);
      family.insert(QStringLiteral("virtualCopyCount"), virtual_copy_asset_ids.size());
      auto variant_names = family.value(QStringLiteral("variantNames")).toStringList();
      const auto variant_name = report.value(QStringLiteral("variantName")).toString();
      if (!variant_name.isEmpty()) {
        variant_names.append(variant_name);
      }
      family.insert(QStringLiteral("variantNames"), variant_names);
    }
    family_by_id.insert(family_id, family);
  }

  QVariantList families;
  for (const auto& family_id : family_order) {
    families.append(family_by_id.value(family_id).toMap());
  }
  return families;
}

QVariantMap batch_summary(
    const QString& scope,
    const QStringList& asset_ids,
    const QVariantList& asset_reports,
    bool draft_active,
    bool confirmation_required) {
  QVariantMap status_counts;
  QVariantList asset_summaries;
  auto warning_count = 0;
  auto error_count = 0;
  auto proof_count = 0;
  auto recipe_diff_count = 0;
  auto mask_local_proof_required_count = 0;
  auto mask_local_proof_ready_count = 0;
  QVariantMap cluster_summaries_by_id;
  QStringList cluster_order;

  for (auto index = 0; index < asset_reports.size(); ++index) {
    const auto report = asset_reports.at(index).toMap();
    const auto status = report.value(QStringLiteral("status"), QStringLiteral("pending")).toString();
    status_counts.insert(status, status_counts.value(status).toInt() + 1);
    const auto warnings = report.value(QStringLiteral("warnings")).toStringList();
    const auto errors = report.value(QStringLiteral("errors")).toStringList();
    warning_count += warnings.size();
    error_count += errors.size();

    const auto validation_report = report.value(QStringLiteral("validationReport")).toMap();
    const auto artifacts = validation_report.value(QStringLiteral("artifacts")).toMap().isEmpty()
                               ? report.value(QStringLiteral("artifacts")).toMap()
                               : validation_report.value(QStringLiteral("artifacts")).toMap();
    const auto has_proof =
        !artifacts.value(QStringLiteral("afterProofPath")).toString().isEmpty() ||
        !artifacts.value(QStringLiteral("currentProofPath")).toString().isEmpty();
    const auto has_recipe_diff = !report.value(QStringLiteral("recipeDiff")).toMap().isEmpty() ||
                                 !validation_report.value(QStringLiteral("recipeDiff")).toMap().isEmpty();
    if (has_proof) {
      ++proof_count;
    }
    if (has_recipe_diff) {
      ++recipe_diff_count;
    }
    const auto mask_local_proof = report.value(QStringLiteral("maskLocalProof")).toMap();
    if (mask_local_proof.value(QStringLiteral("required")).toBool()) {
      ++mask_local_proof_required_count;
      if (mask_local_proof.value(QStringLiteral("proofAvailable")).toBool()) {
        ++mask_local_proof_ready_count;
      }
    }
    const auto cluster_id = report.value(QStringLiteral("clusterId")).toString();
    if (!cluster_id.isEmpty()) {
      if (!cluster_summaries_by_id.contains(cluster_id)) {
        cluster_order.append(cluster_id);
        cluster_summaries_by_id.insert(
            cluster_id,
            QVariantMap{
                {QStringLiteral("clusterId"), cluster_id},
                {QStringLiteral("representativeAssetId"),
                 report.value(QStringLiteral("clusterRepresentativeAssetId"))},
                {QStringLiteral("assetIds"), QStringList{}},
                {QStringLiteral("statusCounts"), QVariantMap{}},
                {QStringLiteral("syncGroups"), report.value(QStringLiteral("clusterSyncGroups"))},
                {QStringLiteral("outlierAssetIds"), QStringList{}},
            });
      }
      auto cluster_summary = cluster_summaries_by_id.value(cluster_id).toMap();
      auto cluster_asset_ids = cluster_summary.value(QStringLiteral("assetIds")).toStringList();
      const auto asset_id = report.value(
          QStringLiteral("assetId"),
          index < asset_ids.size() ? asset_ids.at(index) : QString()).toString();
      cluster_asset_ids.append(asset_id);
      cluster_summary.insert(QStringLiteral("assetIds"), cluster_asset_ids);
      cluster_summary.insert(QStringLiteral("assetCount"), cluster_asset_ids.size());
      auto cluster_status_counts = cluster_summary.value(QStringLiteral("statusCounts")).toMap();
      cluster_status_counts.insert(status, cluster_status_counts.value(status).toInt() + 1);
      cluster_summary.insert(QStringLiteral("statusCounts"), cluster_status_counts);
      if (report.value(QStringLiteral("revisionCount")).toInt() > 0 ||
          report.value(QStringLiteral("validationIterationLimitReached")).toBool()) {
        auto outlier_ids = cluster_summary.value(QStringLiteral("outlierAssetIds")).toStringList();
        outlier_ids.append(asset_id);
        cluster_summary.insert(QStringLiteral("outlierAssetIds"), outlier_ids);
        cluster_summary.insert(QStringLiteral("outlierCorrectionCount"), outlier_ids.size());
      }
      cluster_summaries_by_id.insert(cluster_id, cluster_summary);
    }

    asset_summaries.append(QVariantMap{
        {QStringLiteral("assetId"),
         report.value(
             QStringLiteral("assetId"),
             index < asset_ids.size() ? asset_ids.at(index) : QString()).toString()},
        {QStringLiteral("status"), status},
        {QStringLiteral("warningCount"), warnings.size()},
        {QStringLiteral("errorCount"), errors.size()},
        {QStringLiteral("hasProof"), has_proof},
        {QStringLiteral("hasRecipeDiff"), has_recipe_diff},
        {QStringLiteral("hasMaskLocalProof"),
         mask_local_proof.value(QStringLiteral("proofAvailable")).toBool()},
        {QStringLiteral("clusterId"), cluster_id},
        {QStringLiteral("clusterRepresentativeAssetId"),
         report.value(QStringLiteral("clusterRepresentativeAssetId"))},
    });
  }

  QVariantList cluster_summaries;
  for (const auto& cluster_id : cluster_order) {
    cluster_summaries.append(cluster_summaries_by_id.value(cluster_id).toMap());
  }

  const auto asset_count = asset_ids.isEmpty() ? asset_reports.size() : asset_ids.size();
  const auto variant_families = variant_family_summaries(asset_reports);
  return QVariantMap{
      {QStringLiteral("scope"), scope},
      {QStringLiteral("assetCount"), asset_count},
      {QStringLiteral("assetIds"), asset_ids},
      {QStringLiteral("variantFamilies"), variant_families},
      {QStringLiteral("virtualCopyPolicy"), QStringLiteral("target-asset-id-only")},
      {QStringLiteral("clusterCount"), cluster_summaries.size()},
      {QStringLiteral("clusterSummaries"), cluster_summaries},
      {QStringLiteral("statusCounts"), status_counts},
      {QStringLiteral("warningCount"), warning_count},
      {QStringLiteral("errorCount"), error_count},
      {QStringLiteral("proofCount"), proof_count},
      {QStringLiteral("recipeDiffCount"), recipe_diff_count},
      {QStringLiteral("maskLocalProofRequiredCount"), mask_local_proof_required_count},
      {QStringLiteral("maskLocalProofReadyCount"), mask_local_proof_ready_count},
      {QStringLiteral("draftActive"), draft_active},
      {QStringLiteral("readyForCommit"), draft_active && asset_count > 0 && error_count == 0},
      {QStringLiteral("confirmationRequired"), confirmation_required},
      {QStringLiteral("concurrencyLimit"), kAgentBatchConcurrencyLimit},
      {QStringLiteral("assetSummaries"), asset_summaries},
  };
}

QVariantMap mask_local_proof_report(
    const QVariantMap& patch,
    const QVariantMap& validation_report) {
  const auto masks_patch = patch.value(QStringLiteral("masks")).toMap();
  const auto mask_stack = masks_patch.value(QStringLiteral("maskStack")).toList();
  const QStringList legacy_mask_fields{
      QStringLiteral("linearMaskExposure"),
      QStringLiteral("linearMaskTemperature"),
      QStringLiteral("linearMaskFeather"),
      QStringLiteral("linearMaskPosition"),
      QStringLiteral("linearMaskRotation"),
      QStringLiteral("radialMaskExposure"),
      QStringLiteral("radialMaskTemperature"),
      QStringLiteral("radialMaskFeather"),
      QStringLiteral("radialMaskCenterX"),
      QStringLiteral("radialMaskCenterY"),
      QStringLiteral("radialMaskRadiusX"),
      QStringLiteral("radialMaskRadiusY"),
      QStringLiteral("radialMaskRotation"),
  };
  const QStringList local_adjustment_fields{
      QStringLiteral("exposure"),
      QStringLiteral("contrast"),
      QStringLiteral("highlights"),
      QStringLiteral("shadows"),
      QStringLiteral("whites"),
      QStringLiteral("blacks"),
      QStringLiteral("temperature"),
      QStringLiteral("tint"),
      QStringLiteral("vibrance"),
      QStringLiteral("saturation"),
      QStringLiteral("hueShift"),
      QStringLiteral("saturationShift"),
      QStringLiteral("texture"),
      QStringLiteral("clarity"),
      QStringLiteral("dehaze"),
      QStringLiteral("sharpening"),
      QStringLiteral("luminanceNr"),
      QStringLiteral("colorNr"),
      QStringLiteral("opacity"),
      QStringLiteral("density"),
      QStringLiteral("blendMode"),
      QStringLiteral("colorizeStrength"),
  };
  auto legacy_field_count = 0;
  for (const auto& field : legacy_mask_fields) {
    if (masks_patch.contains(field)) {
      ++legacy_field_count;
    }
  }
  auto local_field_count = 0;
  QStringList mask_kinds;
  for (const auto& mask_value : mask_stack) {
    const auto mask = mask_value.toMap();
    const auto kind = mask.value(QStringLiteral("kind")).toString();
    if (!kind.isEmpty() && !mask_kinds.contains(kind)) {
      mask_kinds.append(kind);
    }
    for (const auto& field : local_adjustment_fields) {
      if (mask.contains(field)) {
        ++local_field_count;
      }
    }
  }
  const auto mask_stack_count = static_cast<int>(mask_stack.size());
  const auto mask_edit_count = legacy_field_count + mask_stack_count;
  const auto has_mask_or_local_edit = mask_edit_count > 0 || local_field_count > 0;
  const auto artifacts = validation_report.value(QStringLiteral("artifacts")).toMap();
  return QVariantMap{
      {QStringLiteral("required"), has_mask_or_local_edit},
      {QStringLiteral("uiProofSurface"), QStringLiteral("agent-dialog-before-after-proof")},
      {QStringLiteral("proofAvailable"),
       !artifacts.value(QStringLiteral("beforeProofPath")).toString().isEmpty() &&
           !artifacts.value(QStringLiteral("afterProofPath")).toString().isEmpty()},
      {QStringLiteral("validationReportPath"),
       artifacts.value(QStringLiteral("validationReportPath"))},
      {QStringLiteral("beforeProofPath"), artifacts.value(QStringLiteral("beforeProofPath"))},
      {QStringLiteral("afterProofPath"), artifacts.value(QStringLiteral("afterProofPath"))},
      {QStringLiteral("maskEditCount"), mask_edit_count},
      {QStringLiteral("maskStackCount"), mask_stack_count},
      {QStringLiteral("legacyMaskFieldCount"), legacy_field_count},
      {QStringLiteral("localAdjustmentFieldCount"), local_field_count},
      {QStringLiteral("maskKinds"), mask_kinds},
  };
}

QVariantMap batch_transaction(
    const QString& transaction_id,
    const QString& scope,
    const QStringList& asset_ids,
    const QString& status,
    bool rollback_available) {
  return QVariantMap{
      {QStringLiteral("transactionId"), transaction_id},
      {QStringLiteral("scope"), scope},
      {QStringLiteral("assetIds"), asset_ids},
      {QStringLiteral("assetCount"), asset_ids.size()},
      {QStringLiteral("status"), status},
      {QStringLiteral("rollbackAvailable"), rollback_available},
      {QStringLiteral("atomicSelection"), asset_ids.size() > 1},
  };
}

void insert_cancel_rollback_metadata(
    QVariantMap* report,
    const QString& run_id,
    const QString& scope,
    const QStringList& asset_ids,
    bool rollback_requested,
    int applied_count,
    int restored_count) {
  if (report == nullptr) {
    return;
  }
  const auto rollback_status =
      restored_count == applied_count
          ? (applied_count > 0 ? QStringLiteral("restored") : QStringLiteral("nothing_applied"))
          : QStringLiteral("restore_incomplete");
  report->insert(QStringLiteral("rollbackOnCancel"), rollback_requested);
  report->insert(QStringLiteral("partialDraftRollback"), rollback_status);
  report->insert(QStringLiteral("appliedCount"), applied_count);
  report->insert(QStringLiteral("restoredCount"), restored_count);
  report->insert(QStringLiteral("draftActive"), false);
  report->insert(
      QStringLiteral("batchTransaction"),
      batch_transaction(
          run_id,
          scope,
          asset_ids,
          rollback_status == QStringLiteral("restore_incomplete")
              ? QStringLiteral("cancel_restore_incomplete")
              : QStringLiteral("canceled_rolled_back"),
          false));
}

QVariantMap run_history_entry(const QVariantMap& report) {
  const auto mode = report.value(QStringLiteral("mode")).toString();
  const auto run_id = report.value(QStringLiteral("runId")).toString();
  if (mode.isEmpty() && run_id.isEmpty()) {
    return {};
  }
  const auto asset_ids = report.value(QStringLiteral("assetIds")).toStringList();
  const auto asset_count = asset_ids.isEmpty()
                               ? std::max(
                                     report.value(QStringLiteral("assetCount")).toInt(),
                                     std::max(
                                         report.value(QStringLiteral("draftedCount")).toInt(),
                                         report.value(QStringLiteral("checkedCount")).toInt()))
                               : asset_ids.size();
  return QVariantMap{
      {QStringLiteral("runId"), run_id},
      {QStringLiteral("mode"), mode},
      {QStringLiteral("ok"), report.value(QStringLiteral("ok"))},
      {QStringLiteral("status"), report.value(QStringLiteral("ok")).toBool()
                                     ? QStringLiteral("ok")
                                     : QStringLiteral("failed")},
      {QStringLiteral("scope"), report.value(QStringLiteral("scope"))},
      {QStringLiteral("assetCount"), asset_count},
      {QStringLiteral("styleId"), report.value(QStringLiteral("styleId"))},
      {QStringLiteral("modelId"), report.value(QStringLiteral("modelId"))},
      {QStringLiteral("startedAtUtc"), report.value(QStringLiteral("startedAtUtc"))},
      {QStringLiteral("finishedAtUtc"), report.value(QStringLiteral("finishedAtUtc"))},
      {QStringLiteral("durationMs"), report.value(QStringLiteral("durationMs"))},
      {QStringLiteral("queueWaitMs"), report.value(QStringLiteral("queueWaitMs"))},
      {QStringLiteral("performanceBudget"), report.value(QStringLiteral("performanceBudget"))},
      {QStringLiteral("manifestPath"), report.value(QStringLiteral("manifestPath"))},
      {QStringLiteral("shareableManifestPath"), report.value(QStringLiteral("shareableManifestPath"))},
      {QStringLiteral("artifactDirectory"), report.value(QStringLiteral("artifactDirectory"))},
      {QStringLiteral("warningCount"), report.value(QStringLiteral("warnings")).toStringList().size()},
      {QStringLiteral("errorCount"), report.value(QStringLiteral("errors")).toStringList().size()},
  };
}

QVariantList selected_asset_maps(const CatalogAssetModel* catalog_model) {
  QVariantList assets;
  if (catalog_model == nullptr) {
    return assets;
  }
  const auto selected_ids = catalog_model->selectedAssetIds();
  QSet<QString> selected_id_set;
  for (const auto& selected_id : selected_ids) {
    selected_id_set.insert(selected_id);
  }
  for (auto index = 0; index < catalog_model->assetCount(); ++index) {
    const auto asset = catalog_model->assetAt(index);
    const auto asset_id = asset.value(QStringLiteral("assetId")).toString();
    if (selected_id_set.contains(asset_id)) {
      assets.append(asset);
    }
  }
  return assets;
}

QString normalized_agent_scope(const QString& scope) {
  auto normalized = scope.trimmed().toLower();
  normalized.replace(QLatin1Char('_'), QLatin1Char('-'));
  if (normalized.isEmpty() || normalized == QStringLiteral("selected")) {
    return QStringLiteral("selected-assets");
  }
  if (normalized == QStringLiteral("current") || normalized == QStringLiteral("photo")) {
    return QStringLiteral("current-photo");
  }
  if (normalized == QStringLiteral("filter") || normalized == QStringLiteral("filtered")) {
    return QStringLiteral("filtered-library");
  }
  if (normalized == QStringLiteral("album") || normalized == QStringLiteral("project")) {
    return QStringLiteral("project");
  }
  return normalized;
}

QVariantList scoped_asset_maps(const CatalogAssetModel* catalog_model, const QString& scope) {
  QVariantList assets;
  if (catalog_model == nullptr) {
    return assets;
  }

  const auto normalized_scope = normalized_agent_scope(scope);
  if (normalized_scope == QStringLiteral("current-photo")) {
    const auto selected = catalog_model->assetAt(catalog_model->selectedIndex());
    if (!selected.isEmpty()) {
      assets.append(selected);
    }
    return assets;
  }
  if (normalized_scope == QStringLiteral("filtered-library")) {
    return catalog_model->visibleAssets();
  }
  if (normalized_scope == QStringLiteral("project")) {
    const auto project_name = catalog_model->projectFilter();
    return catalog_model->assetsForProject(project_name == QStringLiteral("all")
                                               ? QStringLiteral("all")
                                               : project_name);
  }
  return selected_asset_maps(catalog_model);
}

QString report_scope_for_assets(
    const QString& requested_scope,
    const QVariantList& assets) {
  const auto normalized_scope = normalized_agent_scope(requested_scope);
  if (normalized_scope == QStringLiteral("selected-assets")) {
    return assets.size() > 1 ? QStringLiteral("selected-assets") : QStringLiteral("current-photo");
  }
  return normalized_scope;
}

QStringList asset_ids_from_maps(const QVariantList& assets) {
  QStringList asset_ids;
  for (const auto& asset_value : assets) {
    const auto asset_id = asset_value.toMap().value(QStringLiteral("assetId")).toString();
    if (!asset_id.isEmpty()) {
      asset_ids.append(asset_id);
    }
  }
  return asset_ids;
}

QString empty_scope_error(const QString& scope, const QString& action) {
  const auto normalized_scope = normalized_agent_scope(scope);
  if (normalized_scope == QStringLiteral("project")) {
    return QStringLiteral("No photos are available in the current project/album scope for %1.").arg(action);
  }
  if (normalized_scope == QStringLiteral("filtered-library")) {
    return QStringLiteral("No photos are available in the current filtered library scope for %1.").arg(action);
  }
  if (normalized_scope == QStringLiteral("current-photo")) {
    return QStringLiteral("No current photo is available for %1.").arg(action);
  }
  return QStringLiteral("No selected photos are available for %1.").arg(action);
}

int metric_int(const QVariantMap& metrics, const QString& key) {
  return std::max(0, metrics.value(key).toInt());
}

void adjust_patch_number(
    QVariantMap* patch,
    const QString& group,
    const QString& field,
    double delta,
    double minimum,
    double maximum) {
  if (patch == nullptr) {
    return;
  }
  auto group_map = patch->value(group).toMap();
  const auto current = group_map.value(field, 0.0).toDouble();
  group_map.insert(field, std::clamp(current + delta, minimum, maximum));
  patch->insert(group, group_map);
}

QVariantMap revise_recipe_patch_from_validation(
    QVariantMap patch,
    const QVariantMap& validation_report) {
  const auto warnings = validation_report.value(QStringLiteral("warnings")).toStringList();
  const auto joined = warnings.join(QStringLiteral("; "));
  if (joined.contains(QStringLiteral("highlight clipping"), Qt::CaseInsensitive)) {
    adjust_patch_number(
        &patch,
        QStringLiteral("globalTone"),
        QStringLiteral("exposure"),
        -0.30,
        -5.0,
        5.0);
    adjust_patch_number(
        &patch,
        QStringLiteral("globalTone"),
        QStringLiteral("highlights"),
        -12.0,
        -100.0,
        100.0);
    adjust_patch_number(
        &patch,
        QStringLiteral("globalTone"),
        QStringLiteral("whites"),
        -8.0,
        -100.0,
        100.0);
  }
  if (joined.contains(QStringLiteral("shadow clipping"), Qt::CaseInsensitive)) {
    adjust_patch_number(
        &patch,
        QStringLiteral("globalTone"),
        QStringLiteral("exposure"),
        0.20,
        -5.0,
        5.0);
    adjust_patch_number(
        &patch,
        QStringLiteral("globalTone"),
        QStringLiteral("shadows"),
        12.0,
        -100.0,
        100.0);
    adjust_patch_number(
        &patch,
        QStringLiteral("globalTone"),
        QStringLiteral("blacks"),
        8.0,
        -100.0,
        100.0);
  }
  if (joined.contains(QStringLiteral("extreme saturation"), Qt::CaseInsensitive)) {
    adjust_patch_number(
        &patch,
        QStringLiteral("color"),
        QStringLiteral("saturation"),
        -10.0,
        -100.0,
        100.0);
    adjust_patch_number(
        &patch,
        QStringLiteral("color"),
        QStringLiteral("vibrance"),
        -8.0,
        -100.0,
        100.0);
  }
  if (joined.contains(QStringLiteral("channel imbalance"), Qt::CaseInsensitive)) {
    adjust_patch_number(
        &patch,
        QStringLiteral("color"),
        QStringLiteral("temperature"),
        -3.0,
        -100.0,
        100.0);
    adjust_patch_number(
        &patch,
        QStringLiteral("color"),
        QStringLiteral("tint"),
        -2.0,
        -100.0,
        100.0);
    adjust_patch_number(
        &patch,
        QStringLiteral("color"),
        QStringLiteral("saturation"),
        -5.0,
        -100.0,
        100.0);
  }
  return patch;
}

QString run_artifact_directory(const QString& run_id) {
  return QDir(AppBootstrap::appDataLocation())
      .filePath(QStringLiteral("agent-runs/%1").arg(safe_artifact_id(run_id, QStringLiteral("run"))));
}

QString redacted_path_string(const QString& value) {
  const auto trimmed = value.trimmed();
  if (trimmed.isEmpty()) {
    return value;
  }

  const auto redact_root = [&](const QString& root, const QString& token) {
    const auto clean_root = QDir::cleanPath(root);
    if (clean_root.isEmpty() || clean_root == QStringLiteral(".")) {
      return QString();
    }
    const auto clean_value = QDir::cleanPath(trimmed);
    if (clean_value == clean_root || clean_value.startsWith(clean_root + QLatin1Char('/'))) {
      return token + clean_value.sliced(clean_root.size());
    }
    return QString();
  };

  const QStringList known_roots = {
      AppBootstrap::appDataLocation(),
      QDir::homePath(),
      QDir::tempPath(),
  };
  const QStringList known_tokens = {
      QStringLiteral("<app-data>"),
      QStringLiteral("<home>"),
      QStringLiteral("<temp>"),
  };
  for (int index = 0; index < known_roots.size(); ++index) {
    const auto redacted = redact_root(known_roots.at(index), known_tokens.at(index));
    if (!redacted.isEmpty()) {
      return redacted;
    }
  }

  const auto file_url_prefix = QStringLiteral("file://");
  if (trimmed.startsWith(file_url_prefix)) {
    const auto path = trimmed.sliced(file_url_prefix.size());
    const auto redacted = redacted_path_string(path);
    return redacted == path ? value : file_url_prefix + redacted;
  }

  if (trimmed.startsWith(QLatin1Char('/'))) {
    const QFileInfo info(trimmed);
    return QStringLiteral("<absolute-path>/%1").arg(info.fileName());
  }
  return value;
}

QVariant redacted_shareable_value(const QVariant& value) {
  const auto type_id = value.metaType().id();
  if (type_id == QMetaType::QString) {
    return redacted_path_string(value.toString());
  }
  if (type_id == QMetaType::QStringList) {
    QVariantList redacted;
    for (const auto& entry : value.toStringList()) {
      redacted.append(redacted_path_string(entry));
    }
    return redacted;
  }
  if (type_id == QMetaType::QVariantList) {
    QVariantList redacted;
    for (const auto& entry : value.toList()) {
      redacted.append(redacted_shareable_value(entry));
    }
    return redacted;
  }
  if (type_id == QMetaType::QVariantMap) {
    QVariantMap redacted;
    const auto map = value.toMap();
    for (auto iterator = map.cbegin(); iterator != map.cend(); ++iterator) {
      redacted.insert(iterator.key(), redacted_shareable_value(iterator.value()));
    }
    return redacted;
  }
  return value;
}

QVariantMap redacted_shareable_report(const QVariantMap& report) {
  return redacted_shareable_value(report).toMap();
}

QVariantMap model_metadata_by_id(const QVariantList& models, const QString& model_id) {
  for (const auto& value : models) {
    const auto model = value.toMap();
    if (model.value(QStringLiteral("id")).toString() == model_id) {
      return model;
    }
  }
  return QVariantMap{{QStringLiteral("id"), model_id}};
}

QVariantMap style_manifest_metadata(const QVariantMap& style) {
  return QVariantMap{
      {QStringLiteral("id"), style.value(QStringLiteral("id"))},
      {QStringLiteral("name"), style.value(QStringLiteral("name"))},
      {QStringLiteral("schemaVersion"), style.value(QStringLiteral("schemaVersion"))},
      {QStringLiteral("source"), style.value(QStringLiteral("source"))},
  };
}

QVariantMap model_manifest_metadata(const QVariantMap& model, const QString& model_id) {
  return QVariantMap{
      {QStringLiteral("id"), model.value(QStringLiteral("id"), model_id)},
      {QStringLiteral("name"), model.value(QStringLiteral("name"))},
      {QStringLiteral("backend"), model.value(QStringLiteral("backend"))},
      {QStringLiteral("available"), model.value(QStringLiteral("available"))},
  };
}

QVariantMap tool_call_record(
    const QString& name,
    const QString& status,
    const QVariantMap& outcome = QVariantMap()) {
  QVariantMap record{
      {QStringLiteral("name"), name},
      {QStringLiteral("status"), status},
      {QStringLiteral("recordedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
  };
  if (!outcome.isEmpty()) {
    record.insert(QStringLiteral("outcome"), outcome);
  }
  return record;
}

void append_tool_call(
    QVariantList* tool_calls,
    const QString& name,
    const QString& status,
    const QVariantMap& outcome = QVariantMap()) {
  if (tool_calls == nullptr) {
    return;
  }
  tool_calls->append(tool_call_record(name, status, outcome));
}

QVariantList report_tool_calls(const QVariantMap& report) {
  return report.value(QStringLiteral("toolCalls")).toList();
}

void log_agent_run_started(
    const QString& run_id,
    const QString& mode,
    const QString& scope,
    qint64 asset_count,
    const QString& style_id,
    const QString& model_id) {
  observability::logInfo(
      photoEditorAppLog(),
      QStringLiteral("agent.edit.run_started"),
      {{QStringLiteral("run_id"), run_id},
       {QStringLiteral("mode"), mode},
       {QStringLiteral("scope"), scope},
       {QStringLiteral("asset_count"), asset_count},
       {QStringLiteral("style_id"), style_id},
       {QStringLiteral("model_id"), model_id}});
}

void log_agent_tool_calls(const QString& run_id, const QVariantList& tool_calls) {
  for (const auto& value : tool_calls) {
    const auto tool_call = value.toMap();
    observability::logInfo(
        photoEditorAppLog(),
        QStringLiteral("agent.edit.tool_call"),
        {{QStringLiteral("run_id"), run_id},
         {QStringLiteral("name"), tool_call.value(QStringLiteral("name"))},
         {QStringLiteral("status"), tool_call.value(QStringLiteral("status"))},
         {QStringLiteral("outcome"), tool_call.value(QStringLiteral("outcome"))}});
  }
}

int report_asset_count_for_budget(const QVariantMap& report) {
  const auto asset_ids = report.value(QStringLiteral("assetIds")).toStringList();
  if (!asset_ids.isEmpty()) {
    return static_cast<int>(asset_ids.size());
  }
  return std::max(
      1,
      std::max(
          report.value(QStringLiteral("assetCount")).toInt(),
          std::max(
              report.value(QStringLiteral("draftedCount")).toInt(),
              std::max(
                  report.value(QStringLiteral("checkedCount")).toInt(),
                  report.value(QStringLiteral("expectedCount")).toInt()))));
}

QVariantMap performance_budget_report(const QVariantMap& report, qint64 duration_ms) {
  const auto asset_count = report_asset_count_for_budget(report);
  const auto budget_ms =
      kInteractiveAgentRunBudgetMs +
      std::max(0, asset_count - 1) * kInteractiveAgentPerExtraAssetBudgetMs;
  const auto bounded_duration = std::max<qint64>(0, duration_ms);
  return QVariantMap{
      {QStringLiteral("kind"), QStringLiteral("interactive-agent-run")},
      {QStringLiteral("assetCount"), asset_count},
      {QStringLiteral("budgetMs"), budget_ms},
      {QStringLiteral("durationMs"), bounded_duration},
      {QStringLiteral("withinBudget"), bounded_duration <= budget_ms},
      {QStringLiteral("status"),
       bounded_duration <= budget_ms ? QStringLiteral("within-budget")
                                     : QStringLiteral("over-budget")},
  };
}

QVariantMap performance_proof_gate_report(
    const QVariantMap& report,
    const QVariantMap& performance_budget,
    const QVariantList& tool_calls) {
  const auto batch_summary = report.value(QStringLiteral("batchSummary")).toMap();
  const auto artifacts = report.value(QStringLiteral("artifacts")).toMap();
  const auto has_top_level_proof =
      !report.value(QStringLiteral("currentProofPath")).toString().isEmpty() ||
      !report.value(QStringLiteral("afterProofPath")).toString().isEmpty() ||
      !artifacts.value(QStringLiteral("currentProofPath")).toString().isEmpty() ||
      !artifacts.value(QStringLiteral("afterProofPath")).toString().isEmpty();
  const auto proof_count = std::max(
      batch_summary.value(QStringLiteral("proofCount")).toInt(),
      has_top_level_proof ? 1 : 0);
  const auto passed = performance_budget.value(QStringLiteral("withinBudget")).toBool();
  return QVariantMap{
      {QStringLiteral("gate"), QStringLiteral("interactive-agent-performance")},
      {QStringLiteral("required"), true},
      {QStringLiteral("passed"), passed},
      {QStringLiteral("status"),
       passed ? QStringLiteral("passed") : QStringLiteral("failed")},
      {QStringLiteral("budgetMs"), performance_budget.value(QStringLiteral("budgetMs"))},
      {QStringLiteral("durationMs"), performance_budget.value(QStringLiteral("durationMs"))},
      {QStringLiteral("queueWaitMs"), report.value(QStringLiteral("queueWaitMs"))},
      {QStringLiteral("assetCount"), performance_budget.value(QStringLiteral("assetCount"))},
      {QStringLiteral("toolCallCount"), static_cast<int>(tool_calls.size())},
      {QStringLiteral("proofCount"), proof_count},
      {QStringLiteral("evidence"),
       QStringLiteral("duration, queue wait, asset count, tool calls, and proof count are recorded before manifest write")},
  };
}

QVariantMap background_work_policy_report(bool manual_editing_active) {
  return QVariantMap{
      {QStringLiteral("manualEditingActive"), manual_editing_active},
      {QStringLiteral("policy"), QStringLiteral("pause-agent-work-while-manual-editing")},
      {QStringLiteral("action"),
       manual_editing_active ? QStringLiteral("deferred")
                             : QStringLiteral("run")},
      {QStringLiteral("priority"),
       manual_editing_active ? QStringLiteral("manual-editing")
                             : QStringLiteral("agent")},
      {QStringLiteral("resumeCondition"), QStringLiteral("manualEditingActive=false")},
  };
}

void finalize_timing(
    QVariantMap* report,
    const QDateTime& started_at_utc,
    qint64 duration_ms,
    const QVariantList& tool_calls) {
  if (report == nullptr) {
    return;
  }
  report->insert(QStringLiteral("startedAtUtc"), started_at_utc.toString(Qt::ISODateWithMs));
  report->insert(QStringLiteral("finishedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
  report->insert(QStringLiteral("durationMs"), std::max<qint64>(0, duration_ms));
  if (!report->contains(QStringLiteral("queueWaitMs"))) {
    report->insert(QStringLiteral("queueWaitMs"), 0);
  }
  const auto performance_budget =
      performance_budget_report(*report, std::max<qint64>(0, duration_ms));
  report->insert(QStringLiteral("performanceBudget"), performance_budget);
  report->insert(
      QStringLiteral("performanceProofGate"),
      performance_proof_gate_report(*report, performance_budget, tool_calls));
  if (!performance_budget.value(QStringLiteral("withinBudget")).toBool()) {
    auto warnings = report->value(QStringLiteral("warnings")).toStringList();
    warnings.append(
        QStringLiteral("Interactive agent run exceeded performance budget: %1 ms > %2 ms.")
            .arg(performance_budget.value(QStringLiteral("durationMs")).toLongLong())
            .arg(performance_budget.value(QStringLiteral("budgetMs")).toInt()));
    report->insert(QStringLiteral("warnings"), warnings);
  }
  report->insert(QStringLiteral("toolCalls"), tool_calls);
}

void mark_agent_draft_review_required(QVariantMap* report) {
  if (report == nullptr) {
    return;
  }
  report->insert(QStringLiteral("reviewRequired"), true);
  report->insert(QStringLiteral("qualityGuarantee"), false);
  report->insert(QStringLiteral("reviewBasis"), QStringLiteral("style-goals-and-validation-gates"));
  report->insert(
      QStringLiteral("reviewRequiredReason"),
      QStringLiteral("Agent drafts are candidate edits optimized against style goals and validation gates; final taste approval remains with the user."));
}

QString normalized_recipe_base_mode(const QString& mode) {
  const auto normalized = mode.trimmed().toLower();
  return normalized == QStringLiteral("neutral") ? QStringLiteral("neutral") : QStringLiteral("current");
}

QString manual_edit_policy_for_base_mode(const QString& mode) {
  return normalized_recipe_base_mode(mode) == QStringLiteral("neutral")
             ? QStringLiteral("replace-current-edits-with-neutral-base")
             : QStringLiteral("preserve-unspecified-current-recipe-fields");
}

EditRecipeState agent_base_state_for_mode(
    const EditRecipeState& original_state,
    const EditRecipeState& history_state,
    const QString& mode) {
  if (normalized_recipe_base_mode(mode) != QStringLiteral("neutral")) {
    return history_state;
  }

  EditRecipeState neutral_state;
  neutral_state.input_color_profile = original_state.input_color_profile;
  neutral_state.working_color_profile = original_state.working_color_profile;
  neutral_state.preview_color_space = original_state.preview_color_space;
  neutral_state.monitor_color_profile = original_state.monitor_color_profile;
  neutral_state.recipe_snapshots = history_state.recipe_snapshots;
  neutral_state.active_history_branch = history_state.active_history_branch;
  return neutral_state;
}

void tag_latest_agent_snapshot(
    EditRecipeState* state,
    const QString& run_id,
    const QString& model_id,
    const QString& style_id,
    const QDateTime& created_at_utc) {
  if (state == nullptr || state->recipe_snapshots.isEmpty()) {
    return;
  }
  auto snapshot = state->recipe_snapshots.last().toMap();
  snapshot.insert(QStringLiteral("agentGenerated"), true);
  snapshot.insert(QStringLiteral("agentRunId"), run_id);
  snapshot.insert(QStringLiteral("agentModelId"), model_id);
  snapshot.insert(QStringLiteral("agentStyleId"), style_id);
  snapshot.insert(QStringLiteral("agentCreatedAtUtc"), created_at_utc.toString(Qt::ISODateWithMs));
  state->recipe_snapshots[state->recipe_snapshots.size() - 1] = snapshot;
}

void annotate_latest_agent_snapshot_diff(
    EditRecipeState* state,
    const QVariantMap& recipe_patch,
    const QVariantMap& recipe_diff) {
  if (state == nullptr || state->recipe_snapshots.isEmpty()) {
    return;
  }
  auto snapshot = state->recipe_snapshots.last().toMap();
  snapshot.insert(QStringLiteral("agentChangeStorage"), QStringLiteral("recipe-diff-first"));
  snapshot.insert(QStringLiteral("agentRecipePatch"), recipe_patch);
  snapshot.insert(QStringLiteral("agentRecipeDiff"), recipe_diff);
  state->recipe_snapshots[state->recipe_snapshots.size() - 1] = snapshot;
}

void write_agent_run_manifest(
    QVariantMap* report,
    const QVariantMap& style,
    const QVariantMap& model,
    const QString& model_id) {
  if (report == nullptr) {
    return;
  }
  auto warnings = report->value(QStringLiteral("warnings")).toStringList();
  const auto run_id = report->value(QStringLiteral("runId")).toString();
  const auto run_directory = run_artifact_directory(run_id);
  const auto manifest_path = QDir(run_directory).filePath(QStringLiteral("manifest.json"));
  const auto shareable_manifest_path =
      QDir(run_directory).filePath(QStringLiteral("shareable-manifest.json"));
  if (!QDir().mkpath(run_directory)) {
    warnings.append(QStringLiteral("Unable to create agent run directory: %1").arg(run_directory));
  }

  const QVariantMap manifest{
      {QStringLiteral("runId"), run_id},
      {QStringLiteral("mode"), report->value(QStringLiteral("mode"))},
      {QStringLiteral("scope"), report->value(QStringLiteral("scope"))},
      {QStringLiteral("assetIds"), report->value(QStringLiteral("assetIds"))},
      {QStringLiteral("style"), style_manifest_metadata(style)},
      {QStringLiteral("model"), model_manifest_metadata(model, model_id)},
      {QStringLiteral("modelRunnerMode"), report->value(QStringLiteral("modelRunnerMode"))},
      {QStringLiteral("uiEventPumpCount"), report->value(QStringLiteral("uiEventPumpCount"))},
      {QStringLiteral("planSummary"), report->value(QStringLiteral("planSummary"))},
      {QStringLiteral("editGoals"), report->value(QStringLiteral("editGoals"))},
      {QStringLiteral("riskAssessment"), report->value(QStringLiteral("riskAssessment"))},
      {QStringLiteral("styleConstraintCitations"),
       report->value(QStringLiteral("styleConstraintCitations"))},
      {QStringLiteral("reviewRequired"), report->value(QStringLiteral("reviewRequired"))},
      {QStringLiteral("qualityGuarantee"), report->value(QStringLiteral("qualityGuarantee"))},
      {QStringLiteral("reviewBasis"), report->value(QStringLiteral("reviewBasis"))},
      {QStringLiteral("reviewRequiredReason"), report->value(QStringLiteral("reviewRequiredReason"))},
      {QStringLiteral("recipeBaseMode"), report->value(QStringLiteral("recipeBaseMode"))},
      {QStringLiteral("manualEditPolicy"), report->value(QStringLiteral("manualEditPolicy"))},
      {QStringLiteral("recipeStorageMode"), report->value(QStringLiteral("recipeStorageMode"))},
      {QStringLiteral("recipePatch"), report->value(QStringLiteral("recipePatch"))},
      {QStringLiteral("plannedRecipePatch"), report->value(QStringLiteral("plannedRecipePatch"))},
      {QStringLiteral("recipeDiff"), report->value(QStringLiteral("recipeDiff"))},
      {QStringLiteral("artifactDirectory"), report->value(QStringLiteral("artifactDirectory"))},
      {QStringLiteral("maskLocalProof"), report->value(QStringLiteral("maskLocalProof"))},
      {QStringLiteral("assetReports"), report->value(QStringLiteral("assetReports"))},
      {QStringLiteral("batchClusters"), report->value(QStringLiteral("batchClusters"))},
      {QStringLiteral("clusterPlanning"), report->value(QStringLiteral("clusterPlanning"))},
      {QStringLiteral("clusterCount"), report->value(QStringLiteral("clusterCount"))},
      {QStringLiteral("representativeAssetIds"), report->value(QStringLiteral("representativeAssetIds"))},
      {QStringLiteral("batchSummary"), report->value(QStringLiteral("batchSummary"))},
      {QStringLiteral("batchTransaction"), report->value(QStringLiteral("batchTransaction"))},
      {QStringLiteral("batchConcurrencyLimit"), report->value(QStringLiteral("batchConcurrencyLimit"))},
      {QStringLiteral("previewProofPolicy"), report->value(QStringLiteral("previewProofPolicy"))},
      {QStringLiteral("rollbackOnCancel"), report->value(QStringLiteral("rollbackOnCancel"))},
      {QStringLiteral("partialDraftRollback"), report->value(QStringLiteral("partialDraftRollback"))},
      {QStringLiteral("appliedCount"), report->value(QStringLiteral("appliedCount"))},
      {QStringLiteral("restoredCount"), report->value(QStringLiteral("restoredCount"))},
      {QStringLiteral("errors"), report->value(QStringLiteral("errors"))},
      {QStringLiteral("warnings"), report->value(QStringLiteral("warnings"))},
      {QStringLiteral("failureReason"), report->value(QStringLiteral("failureReason"))},
      {QStringLiteral("cancellationReason"), report->value(QStringLiteral("cancellationReason"))},
      {QStringLiteral("toolCalls"), report->value(QStringLiteral("toolCalls"))},
      {QStringLiteral("startedAtUtc"), report->value(QStringLiteral("startedAtUtc"))},
      {QStringLiteral("finishedAtUtc"), report->value(QStringLiteral("finishedAtUtc"))},
      {QStringLiteral("durationMs"), report->value(QStringLiteral("durationMs"))},
      {QStringLiteral("queueWaitMs"), report->value(QStringLiteral("queueWaitMs"))},
      {QStringLiteral("performanceBudget"), report->value(QStringLiteral("performanceBudget"))},
      {QStringLiteral("performanceProofGate"), report->value(QStringLiteral("performanceProofGate"))},
      {QStringLiteral("writtenAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
  };
  write_json_artifact(manifest_path, manifest, &warnings);
  auto shareable_manifest = redacted_shareable_report(manifest);
  shareable_manifest.insert(QStringLiteral("redacted"), true);
  shareable_manifest.insert(
      QStringLiteral("redactionPolicy"),
      QStringLiteral("Absolute app-data, home, temp, and source paths are replaced for shareable reports."));
  write_json_artifact(shareable_manifest_path, shareable_manifest, &warnings);
  report->insert(QStringLiteral("warnings"), warnings);
  report->insert(QStringLiteral("runArtifactDirectory"), run_directory);
  report->insert(QStringLiteral("manifestPath"), manifest_path);
  report->insert(QStringLiteral("shareableManifestPath"), shareable_manifest_path);
}

QVariantMap render_agent_analysis_artifacts(
    const QString& file_path,
    const EditRecipeState& state,
    const QVariantMap& style,
    const QString& run_id,
    const QString& asset_id,
    const QVariantMap& base_report) {
  QVariantMap report = base_report;
  QStringList warnings = report.value(QStringLiteral("warnings")).toStringList();
  const auto safe_run_id = safe_artifact_id(run_id, QStringLiteral("analysis"));
  const auto safe_asset_id = safe_artifact_id(asset_id, QStringLiteral("asset"));
  const auto artifact_directory = QDir(AppBootstrap::appDataLocation())
                                      .filePath(QStringLiteral("agent-runs/%1/%2")
                                                    .arg(safe_run_id, safe_asset_id));
  const auto proof_path = QDir(artifact_directory).filePath(QStringLiteral("current-proof.png"));
  const auto analysis_path =
      QDir(artifact_directory).filePath(QStringLiteral("analysis-report.json"));
  QVariantMap artifacts{
      {QStringLiteral("artifactDirectory"), artifact_directory},
      {QStringLiteral("currentProofPath"), proof_path},
      {QStringLiteral("analysisReportPath"), analysis_path},
  };
  if (!QDir().mkpath(artifact_directory)) {
    warnings.append(QStringLiteral("Unable to create agent artifact directory: %1")
                        .arg(artifact_directory));
  }

  QImage source;
  const auto proof_render_source =
      agent_proof_render_source_policy(QStringLiteral("analysis-current-proof"));
  report.insert(QStringLiteral("proofRenderSource"), proof_render_source);
  if (!decode_source_image(file_path, &source, kAgentProofPreviewMaxEdge, state)) {
    warnings.append(QStringLiteral("Unable to decode selected source for agent analysis."));
    report.insert(QStringLiteral("ok"), false);
    report.insert(QStringLiteral("warnings"), warnings);
    report.insert(QStringLiteral("artifacts"), artifacts);
    write_json_artifact(analysis_path, report, &warnings);
    report.insert(QStringLiteral("warnings"), warnings);
    return report;
  }
  const auto rendered = render_output_image(
      source,
      state,
      preview_color_space(),
      QImage::Format_ARGB32);
  if (rendered.isNull()) {
    warnings.append(QStringLiteral("Unable to render current proof for agent analysis."));
    report.insert(QStringLiteral("ok"), false);
    report.insert(QStringLiteral("warnings"), warnings);
    report.insert(QStringLiteral("artifacts"), artifacts);
    write_json_artifact(analysis_path, report, &warnings);
    report.insert(QStringLiteral("warnings"), warnings);
    return report;
  }

  if (!rendered.save(proof_path, "PNG")) {
    warnings.append(QStringLiteral("Unable to save analysis proof: %1").arg(proof_path));
  }
  const auto validation_report = validate_agent_edit_image(rendered, state, style);
  warnings.append(validation_report.value(QStringLiteral("warnings")).toStringList());
  report.insert(QStringLiteral("warnings"), warnings);
  report.insert(QStringLiteral("validationReport"), validation_report);
  report.insert(QStringLiteral("proofRenderSource"), proof_render_source);
  report.insert(QStringLiteral("riskAssessment"), agent_risk_assessment(state, warnings));
  report.insert(QStringLiteral("proofWidth"), rendered.width());
  report.insert(QStringLiteral("proofHeight"), rendered.height());
  report.insert(QStringLiteral("artifacts"), artifacts);
  report.insert(QStringLiteral("artifactDirectory"), artifact_directory);
  report.insert(QStringLiteral("currentProofPath"), proof_path);
  report.insert(QStringLiteral("analysisReportPath"), analysis_path);
  write_json_artifact(analysis_path, report, &warnings);
  report.insert(QStringLiteral("warnings"), warnings);
  return report;
}

QVariantMap render_agent_proof_artifacts(
    const QString& file_path,
    const EditRecipeState& before_state,
    const EditRecipeState& after_state,
    const QVariantMap& style,
    const QString& run_id,
    const QString& asset_id,
    const QVariantMap& patch,
    const ProofCancellationCallback& cancellation_requested) {
  QVariantMap report;
  QStringList warnings;
  const auto safe_run_id = safe_artifact_id(run_id, QStringLiteral("draft"));
  const auto safe_asset_id = safe_artifact_id(asset_id, QStringLiteral("asset"));
  const auto run_directory = QDir(AppBootstrap::appDataLocation())
                                 .filePath(QStringLiteral("agent-runs/%1").arg(safe_run_id));
  const auto artifact_directory = QDir(run_directory).filePath(safe_asset_id);
  const auto before_path = QDir(artifact_directory).filePath(QStringLiteral("before.png"));
  const auto after_path = QDir(artifact_directory).filePath(QStringLiteral("after.png"));
  const auto diff_path = QDir(artifact_directory).filePath(QStringLiteral("recipe-diff.json"));
  const auto validation_path =
      QDir(artifact_directory).filePath(QStringLiteral("validation-report.json"));
  QVariantMap artifacts{
      {QStringLiteral("artifactDirectory"), artifact_directory},
      {QStringLiteral("beforeProofPath"), before_path},
      {QStringLiteral("afterProofPath"), after_path},
      {QStringLiteral("recipeDiffPath"), diff_path},
      {QStringLiteral("validationReportPath"), validation_path},
  };
  const QVariantMap proof_render_policy{
      {QStringLiteral("cancellable"), true},
      {QStringLiteral("timeoutAware"), true},
      {QStringLiteral("checkpoints"),
       QStringList{
           QStringLiteral("before-decode"),
           QStringLiteral("before-before-render"),
           QStringLiteral("before-after-render"),
           QStringLiteral("before-save"),
           QStringLiteral("before-validation"),
       }},
  };
  const auto proof_render_source =
      agent_proof_render_source_policy(QStringLiteral("draft-validation-proof"));
  const auto recipe_diff = agent_recipe_patch_diff(before_state, after_state, patch);
  if (!QDir().mkpath(artifact_directory)) {
    warnings.append(QStringLiteral("Unable to create agent artifact directory: %1")
                        .arg(artifact_directory));
  }

  const auto cancellation_report = [&](const QString& checkpoint) {
    auto canceled_report = result_map(
        false,
        {},
        warnings
            << QStringLiteral("Agent proof rendering canceled at checkpoint: %1.")
                   .arg(checkpoint));
    canceled_report.insert(QStringLiteral("canceled"), true);
    canceled_report.insert(QStringLiteral("cancellationCheckpoint"), checkpoint);
    canceled_report.insert(QStringLiteral("proofRenderPolicy"), proof_render_policy);
    canceled_report.insert(QStringLiteral("proofRenderSource"), proof_render_source);
    canceled_report.insert(QStringLiteral("artifacts"), artifacts);
    canceled_report.insert(QStringLiteral("recipeDiff"), recipe_diff);
    auto cancellation_warnings =
        canceled_report.value(QStringLiteral("warnings")).toStringList();
    write_json_artifact(validation_path, canceled_report, &cancellation_warnings);
    canceled_report.insert(QStringLiteral("warnings"), cancellation_warnings);
    return canceled_report;
  };
  const auto canceled_at = [&](const QString& checkpoint, QVariantMap* output) {
    if (cancellation_requested && cancellation_requested()) {
      *output = cancellation_report(checkpoint);
      return true;
    }
    return false;
  };

  QImage source;
  if (canceled_at(QStringLiteral("before-decode"), &report)) {
    return report;
  }
  if (!decode_source_image(file_path, &source, kAgentProofPreviewMaxEdge, after_state)) {
    warnings.append(QStringLiteral("Unable to decode selected source for agent validation."));
    report.insert(QStringLiteral("ok"), false);
    report.insert(QStringLiteral("warnings"), warnings);
    report.insert(QStringLiteral("artifacts"), artifacts);
    report.insert(QStringLiteral("recipeDiff"), recipe_diff);
    report.insert(QStringLiteral("proofRenderPolicy"), proof_render_policy);
    report.insert(QStringLiteral("proofRenderSource"), proof_render_source);
    return report;
  }
  if (canceled_at(QStringLiteral("before-before-render"), &report)) {
    return report;
  }
  const auto before_rendered = render_output_image(
      source,
      before_state,
      preview_color_space(),
      QImage::Format_ARGB32);
  if (canceled_at(QStringLiteral("before-after-render"), &report)) {
    return report;
  }
  const auto after_rendered = render_output_image(
      source,
      after_state,
      preview_color_space(),
      QImage::Format_ARGB32);
  if (before_rendered.isNull() || after_rendered.isNull()) {
    warnings.append(QStringLiteral("Unable to render draft proof for agent validation."));
    report.insert(QStringLiteral("ok"), false);
    report.insert(QStringLiteral("warnings"), warnings);
    report.insert(QStringLiteral("artifacts"), artifacts);
    report.insert(QStringLiteral("recipeDiff"), recipe_diff);
    report.insert(QStringLiteral("proofRenderPolicy"), proof_render_policy);
    report.insert(QStringLiteral("proofRenderSource"), proof_render_source);
    return report;
  }

  if (canceled_at(QStringLiteral("before-save"), &report)) {
    return report;
  }
  if (!before_rendered.save(before_path, "PNG")) {
    warnings.append(QStringLiteral("Unable to save before proof: %1").arg(before_path));
  }
  if (!after_rendered.save(after_path, "PNG")) {
    warnings.append(QStringLiteral("Unable to save after proof: %1").arg(after_path));
  }
  write_json_artifact(diff_path, recipe_diff, &warnings);

  if (canceled_at(QStringLiteral("before-validation"), &report)) {
    return report;
  }
  report = validate_agent_edit_image(after_rendered, after_state, style);
  auto validation_warnings = report.value(QStringLiteral("warnings")).toStringList();
  validation_warnings.append(warnings);
  report.insert(QStringLiteral("warnings"), validation_warnings);
  report.insert(QStringLiteral("proofRenderPolicy"), proof_render_policy);
  report.insert(QStringLiteral("proofRenderSource"), proof_render_source);
  report.insert(QStringLiteral("proofWidth"), after_rendered.width());
  report.insert(QStringLiteral("proofHeight"), after_rendered.height());
  report.insert(QStringLiteral("artifacts"), artifacts);
  report.insert(QStringLiteral("recipeDiff"), recipe_diff);
  write_json_artifact(validation_path, report, &validation_warnings);
  report.insert(QStringLiteral("warnings"), validation_warnings);
  return report;
}

}  // namespace

AgentEditController::AgentEditController(QObject* parent) : QObject(parent) {
  available_models_ = available_agent_local_models();
  selected_model_id_ = default_agent_local_model_id();
  reloadStyles();
}

void AgentEditController::setCatalogModel(CatalogAssetModel* catalog_model) {
  catalog_model_ = catalog_model;
}

void AgentEditController::setExportJobController(ExportJobController* export_job_controller) {
  export_job_controller_ = export_job_controller;
}

QString AgentEditController::status() const { return status_; }

QString AgentEditController::errorMessage() const { return error_message_; }

bool AgentEditController::running() const { return running_; }

bool AgentEditController::cancellationRequested() const { return cancellation_requested_; }

bool AgentEditController::manualEditingActive() const { return manual_editing_active_; }

int AgentEditController::runTimeoutMs() const { return run_timeout_ms_; }

bool AgentEditController::draftActive() const { return !draft_original_recipes_.isEmpty(); }

QString AgentEditController::selectedStyleId() const { return selected_style_id_; }

QString AgentEditController::selectedModelId() const { return selected_model_id_; }

QString AgentEditController::userInstructions() const { return user_instructions_; }

QString AgentEditController::recipeBaseMode() const { return recipe_base_mode_; }

QVariantList AgentEditController::availableStyles() const { return available_styles_; }

QVariantList AgentEditController::availableModels() const { return available_models_; }

QStringList AgentEditController::availableModelNames() const {
  QStringList names;
  for (const auto& model_value : available_models_) {
    const auto model = model_value.toMap();
    const auto name = model.value(QStringLiteral("name")).toString();
    if (!name.isEmpty()) {
      names.append(name);
    }
  }
  return names;
}

QVariantMap AgentEditController::lastRunReport() const { return last_run_report_; }

QVariantList AgentEditController::runHistory() const { return run_history_; }

void AgentEditController::setSelectedStyleId(const QString& style_id) {
  const auto trimmed = style_id.trimmed();
  if (selected_style_id_ == trimmed) {
    return;
  }
  selected_style_id_ = trimmed;
  emit selectedStyleChanged();
}

void AgentEditController::setSelectedModelId(const QString& model_id) {
  const auto trimmed = model_id.trimmed();
  if (selected_model_id_ == trimmed) {
    return;
  }
  selected_model_id_ = trimmed;
  emit selectedModelChanged();
}

void AgentEditController::setUserInstructions(const QString& instructions) {
  if (user_instructions_ == instructions) {
    return;
  }
  user_instructions_ = instructions;
  emit userInstructionsChanged();
}

void AgentEditController::setRecipeBaseMode(const QString& mode) {
  const auto normalized = normalized_recipe_base_mode(mode);
  if (recipe_base_mode_ == normalized) {
    return;
  }
  recipe_base_mode_ = normalized;
  emit recipeBaseModeChanged();
}

void AgentEditController::setRunTimeoutMs(int timeout_ms) {
  const auto bounded = std::max(-1, timeout_ms);
  if (run_timeout_ms_ == bounded) {
    return;
  }
  run_timeout_ms_ = bounded;
  emit runTimeoutMsChanged();
}

void AgentEditController::setManualEditingActive(bool active) {
  if (manual_editing_active_ == active) {
    return;
  }
  manual_editing_active_ = active;
  emit manualEditingActiveChanged();
}

void AgentEditController::reloadStyles() {
  QStringList load_errors;
  QVariantList styles = built_in_agent_edit_styles();
  const auto user_styles = load_agent_edit_styles_from_directory(styleDirectory(), &load_errors);
  for (const auto& style : user_styles) {
    styles.append(style);
  }
  available_styles_ = styles;
  if (selected_style_id_.isEmpty() && !available_styles_.isEmpty()) {
    selected_style_id_ = available_styles_.first().toMap().value(QStringLiteral("id")).toString();
    emit selectedStyleChanged();
  }
  if (!load_errors.isEmpty()) {
    setErrorMessage(load_errors.join(QStringLiteral("; ")));
  }
  emit availableStylesChanged();
}

QVariantMap AgentEditController::validateStyleJson(const QString& style_json) const {
  const auto validation = validate_agent_edit_style_json(style_json);
  return result_map(validation.valid, validation.errors, validation.warnings);
}

bool AgentEditController::saveStyle(const QVariantMap& style) {
  QString error_message;
  if (!save_agent_edit_style_to_directory(styleDirectory(), style, &error_message)) {
    setErrorMessage(error_message);
    return false;
  }
  reloadStyles();
  return true;
}

QVariantMap AgentEditController::exportStyle(
    const QString& style_id,
    const QString& destination_directory) {
  const auto target_style_id =
      style_id.trimmed().isEmpty() ? selected_style_id_ : style_id.trimmed();
  QVariantMap style;
  for (const auto& value : available_styles_) {
    const auto candidate = value.toMap();
    if (candidate.value(QStringLiteral("id")).toString() == target_style_id) {
      style = candidate;
      break;
    }
  }
  if (style.isEmpty()) {
    return result_map(false, {QStringLiteral("Style is not available for export.")});
  }

  const auto document = style_storage_document_from_runtime_style(style);
  const auto validation =
      validate_agent_edit_style_object(QJsonObject::fromVariantMap(document));
  if (!validation.valid) {
    return result_map(validation.valid, validation.errors, validation.warnings);
  }

  const auto export_directory = destination_directory.trimmed().isEmpty()
                                    ? QDir(AppBootstrap::appDataLocation())
                                          .filePath(QStringLiteral("agent-style-exports"))
                                    : destination_directory.trimmed();
  QDir directory(export_directory);
  if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
    return result_map(false, {QStringLiteral("Unable to create style export directory.")});
  }
  const auto export_path = directory.filePath(
      document.value(QStringLiteral("id")).toString() + QStringLiteral(".json"));
  QSaveFile file(export_path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    return result_map(false, {file.errorString()});
  }
  file.write(QJsonDocument(QJsonObject::fromVariantMap(document)).toJson(QJsonDocument::Indented));
  if (!file.commit()) {
    return result_map(false, {file.errorString()});
  }

  QVariantMap report = result_map(true, {}, validation.warnings);
  report.insert(QStringLiteral("mode"), QStringLiteral("style-export"));
  report.insert(QStringLiteral("styleId"), document.value(QStringLiteral("id")));
  report.insert(QStringLiteral("styleName"), document.value(QStringLiteral("name")));
  report.insert(QStringLiteral("exportPath"), export_path);
  report.insert(QStringLiteral("schemaVersion"), document.value(QStringLiteral("schema_version")));
  setLastRunReport(report);
  return report;
}

QVariantMap AgentEditController::importStyleJson(
    const QString& style_json,
    const QString& name_override) {
  QJsonParseError parse_error;
  const auto parsed = QJsonDocument::fromJson(style_json.toUtf8(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !parsed.isObject()) {
    return result_map(
        false,
        {QStringLiteral("style JSON parse error at offset %1: %2")
             .arg(parse_error.offset)
             .arg(parse_error.errorString())});
  }

  auto object = parsed.object();
  if (!name_override.trimmed().isEmpty()) {
    object.insert(QStringLiteral("name"), name_override.trimmed());
  }
  auto style = object.toVariantMap();
  const auto base_name = style.value(QStringLiteral("name")).toString();
  auto base_id = style.value(QStringLiteral("id")).toString().trimmed();
  if (base_id.isEmpty()) {
    base_id = normalized_style_id(base_name);
  }
  auto style_id = base_id;
  auto suffix = 2;
  const QDir directory(styleDirectory());
  while (QFileInfo::exists(directory.filePath(style_id + QStringLiteral(".json")))) {
    style_id = QStringLiteral("%1-%2").arg(base_id).arg(suffix);
    ++suffix;
  }
  style.insert(QStringLiteral("id"), style_id);

  QString error_message;
  if (!save_agent_edit_style_to_directory(styleDirectory(), style, &error_message)) {
    setErrorMessage(error_message);
    return result_map(false, {error_message});
  }
  selected_style_id_ = style_id;
  emit selectedStyleChanged();
  reloadStyles();

  QVariantMap report = result_map(true);
  report.insert(QStringLiteral("mode"), QStringLiteral("style-import"));
  report.insert(QStringLiteral("styleId"), style_id);
  report.insert(QStringLiteral("styleName"), style.value(QStringLiteral("name")));
  report.insert(QStringLiteral("importPath"), QDir(styleDirectory()).filePath(style_id + QStringLiteral(".json")));
  report.insert(QStringLiteral("renamedOnImport"), style_id != base_id);
  report.insert(QStringLiteral("schemaVersion"), style.value(QStringLiteral("schema_version")));
  setLastRunReport(report);
  return report;
}

QVariantMap AgentEditController::createStyle(
    const QString& name,
    const QString& goal,
    const QVariantMap& allowed_tools,
    const QVariantMap& constraints,
    const QVariantMap& validation,
    const QVariantMap& batch_behavior) {
  const auto trimmed_name = name.trimmed();
  const auto trimmed_goal = goal.trimmed();
  if (trimmed_name.isEmpty() || trimmed_goal.isEmpty()) {
    return result_map(false, {QStringLiteral("Style name and goal are required.")});
  }

  const auto base_id = normalized_style_id(trimmed_name);
  auto style_id = base_id;
  auto suffix = 2;
  const QDir directory(styleDirectory());
  while (QFileInfo::exists(directory.filePath(style_id + QStringLiteral(".json")))) {
    style_id = QStringLiteral("%1-%2").arg(base_id).arg(suffix);
    ++suffix;
  }

  const auto merged_allowed_tools = merged_map(
      allowed_tools,
      QVariantMap{
          {QStringLiteral("raw"), true},
          {QStringLiteral("global_color"), true},
          {QStringLiteral("curves"), true},
          {QStringLiteral("hsl"), true},
          {QStringLiteral("color_grading"), true},
          {QStringLiteral("detail"), true},
          {QStringLiteral("crop"), QStringLiteral("conservative")},
          {QStringLiteral("masks"), false},
          {QStringLiteral("retouch"), false},
          {QStringLiteral("layers_luts"), false},
          {QStringLiteral("export"), false},
      });
  const auto merged_constraints = merged_map(
      constraints,
      QVariantMap{
          {QStringLiteral("avoid_clipped_highlights"), true},
          {QStringLiteral("avoid_crushed_blacks"), true},
          {QStringLiteral("max_exposure_shift"), 1.2},
          {QStringLiteral("max_temperature_shift"), 1800},
          {QStringLiteral("max_saturation_shift"), 18},
      });
  const auto merged_validation = merged_map(
      validation,
      QVariantMap{
          {QStringLiteral("highlight_clip_limit_percent"), 0.5},
          {QStringLiteral("shadow_clip_limit_percent"), 1.0},
          {QStringLiteral("require_before_after_proof"), true},
      });
  const auto merged_batch_behavior = merged_map(
      batch_behavior,
      QVariantMap{
          {QStringLiteral("cluster_by_lighting"), true},
          {QStringLiteral("maintain_sequence_consistency"), true},
          {QStringLiteral("correct_outliers_individually"), true},
      });
  const QVariantMap style{
      {QStringLiteral("schema_version"), kCurrentAgentEditStyleSchemaVersion},
      {QStringLiteral("id"), style_id},
      {QStringLiteral("name"), trimmed_name},
      {QStringLiteral("goal"), trimmed_goal},
      {QStringLiteral("allowed_tools"), merged_allowed_tools},
      {QStringLiteral("constraints"), merged_constraints},
      {QStringLiteral("validation"), merged_validation},
      {QStringLiteral("batch_behavior"), merged_batch_behavior},
  };

  QString error_message;
  if (!save_agent_edit_style_to_directory(styleDirectory(), style, &error_message)) {
    setErrorMessage(error_message);
    return result_map(false, {error_message});
  }
  selected_style_id_ = style_id;
  emit selectedStyleChanged();
  reloadStyles();

  QVariantMap report = result_map(true);
  report.insert(QStringLiteral("styleId"), style_id);
  report.insert(QStringLiteral("style"), style);
  setLastRunReport(report);
  observability::logInfo(
      photoEditorAppLog(),
      QStringLiteral("agent.edit.style_created"),
      {{QStringLiteral("style_id"), style_id},
       {QStringLiteral("allowed_tool_count"), merged_allowed_tools.size()}});
  return report;
}

QVariantMap AgentEditController::validateRecipePatch(const QVariantMap& patch) const {
  const auto validation = validate_agent_recipe_patch(patch, selectedStyle());
  return result_map(validation.valid, validation.errors, validation.warnings);
}

QVariantMap AgentEditController::renderCurrentProofForSelected(
    const QString& mode,
    const QString& start_status,
    const QString& success_status,
    const QString& summary,
    const QString& event_name) {
  QElapsedTimer run_timer;
  run_timer.start();
  const auto started_at_utc = QDateTime::currentDateTimeUtc();
  if (cancellation_requested_) {
    cancellation_requested_ = false;
    rollback_on_cancel_ = false;
    emit cancellationChanged();
  }
  cancellation_reason_.clear();
  setRunning(true);
  setErrorMessage(QString());
  setStatus(start_status);

  QStringList errors;
  if (catalog_model_ == nullptr) {
    errors.append(QStringLiteral("Catalog model is not connected."));
  } else if (catalog_model_->selectedAssetCount() != 1) {
    errors.append(QStringLiteral("Agent proof rendering currently requires exactly one selected photo."));
  }
  const auto style = selectedStyle();
  if (style.isEmpty()) {
    errors.append(QStringLiteral("No valid agent style is selected."));
  }
  if (!errors.isEmpty()) {
    setRunning(false);
    setStatus(QStringLiteral("Failed"));
    setErrorMessage(errors.join(QStringLiteral("; ")));
    const auto report = result_map(false, errors);
    setLastRunReport(report);
    return report;
  }

  const auto asset_id = catalog_model_->selectedAssetId();
  const auto recipe_json = catalog_model_->selectedRecipeJson();
  const auto recipe_validation = validate_edit_recipe_json(recipe_json);
  if (!recipe_validation.valid) {
    errors.append(recipe_validation.errors);
    setRunning(false);
    setStatus(QStringLiteral("Failed"));
    setErrorMessage(errors.join(QStringLiteral("; ")));
    const auto report = result_map(false, errors);
    setLastRunReport(report);
    return report;
  }

  const auto state = parse_edit_recipe_json(recipe_json);
  const auto run_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const auto model = model_metadata_by_id(available_models_, selected_model_id_);
  log_agent_run_started(
      run_id,
      mode,
      QStringLiteral("current-photo"),
      1,
      selected_style_id_,
      selected_model_id_);
  QVariantMap report = result_map(true);
  report.insert(QStringLiteral("mode"), mode);
  report.insert(QStringLiteral("runId"), run_id);
  report.insert(QStringLiteral("assetIds"), QStringList{asset_id});
  report.insert(QStringLiteral("styleId"), selected_style_id_);
  report.insert(QStringLiteral("modelId"), selected_model_id_);
  report.insert(QStringLiteral("modelBackend"), model.value(QStringLiteral("backend")));
  report.insert(QStringLiteral("assetMetadata"), catalog_model_->assetAt(catalog_model_->selectedIndex()));
  report.insert(QStringLiteral("recipeSummary"), recipe_analysis_summary(state, recipe_json));
  report.insert(QStringLiteral("analysisSummary"), summary);
  report.insert(QStringLiteral("editGoals"), agent_edit_goals(style, user_instructions_));

  report = render_agent_analysis_artifacts(
      catalog_model_->selectedFilePath(),
      state,
      style,
      run_id,
      asset_id,
      report);
  QVariantList tool_calls;
  append_tool_call(
      &tool_calls,
      QStringLiteral("render_current_proof"),
      report.value(QStringLiteral("ok")).toBool() ? QStringLiteral("ok") : QStringLiteral("failed"),
      QVariantMap{
          {QStringLiteral("mode"), mode},
          {QStringLiteral("assetId"), asset_id},
          {QStringLiteral("proofPath"), report.value(QStringLiteral("currentProofPath"))},
          {QStringLiteral("validationOk"),
           report.value(QStringLiteral("validationReport")).toMap().value(QStringLiteral("ok"))},
      });
  finalize_timing(&report, started_at_utc, run_timer.elapsed(), tool_calls);
  log_agent_tool_calls(run_id, tool_calls);
  write_agent_run_manifest(&report, style, model, selected_model_id_);
  observability::logInfo(
      photoEditorAppLog(),
      event_name,
      {{QStringLiteral("run_id"), run_id},
       {QStringLiteral("asset_id"), asset_id},
       {QStringLiteral("style_id"), selected_style_id_},
       {QStringLiteral("ok"), report.value(QStringLiteral("ok")).toBool()}});
  setStatus(report.value(QStringLiteral("ok")).toBool() ? success_status : QStringLiteral("Failed"));
  if (!report.value(QStringLiteral("ok")).toBool()) {
    setErrorMessage(report.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("; ")));
  }
  setRunning(false);
  setLastRunReport(report);
  return report;
}

QVariantMap AgentEditController::analyzeSelected() {
  return renderCurrentProofForSelected(
      QStringLiteral("analyze"),
      QStringLiteral("Analyzing"),
      QStringLiteral("Analyzed"),
      QStringLiteral("Analyzed the selected photo without changing its recipe."),
      QStringLiteral("agent.edit.analyzed"));
}

QVariantMap AgentEditController::renderProofForSelected() {
  return renderCurrentProofForSelected(
      QStringLiteral("render-proof"),
      QStringLiteral("Rendering proof"),
      QStringLiteral("Proof rendered"),
      QStringLiteral("Rendered a proof for the selected photo without changing its recipe."),
      QStringLiteral("agent.edit.proof_rendered"));
}

QVariantMap AgentEditController::validateBatchForSelected() {
  return validateBatchForScope(QStringLiteral("selected-assets"));
}

QVariantMap AgentEditController::validateBatchForScope(const QString& scope) {
  QElapsedTimer run_timer;
  run_timer.start();
  const auto started_at_utc = QDateTime::currentDateTimeUtc();
  if (cancellation_requested_) {
    cancellation_requested_ = false;
    emit cancellationChanged();
  }
  cancellation_reason_.clear();
  setRunning(true);
  setErrorMessage(QString());
  setStatus(QStringLiteral("Validating batch"));

  QStringList errors;
  if (catalog_model_ == nullptr) {
    errors.append(QStringLiteral("Catalog model is not connected."));
  }
  const auto style = selectedStyle();
  if (style.isEmpty()) {
    errors.append(QStringLiteral("No valid agent style is selected."));
  }
  const auto selected_assets = scoped_asset_maps(catalog_model_, scope);
  const auto report_scope = report_scope_for_assets(scope, selected_assets);
  if (catalog_model_ != nullptr && selected_assets.isEmpty()) {
    errors.append(empty_scope_error(scope, QStringLiteral("batch validation")));
  }
  if (!errors.isEmpty()) {
    setRunning(false);
    setStatus(QStringLiteral("Failed"));
    setErrorMessage(errors.join(QStringLiteral("; ")));
    auto report = result_map(false, errors);
    setLastRunReport(report);
    return report;
  }

  const auto run_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const auto model = model_metadata_by_id(available_models_, selected_model_id_);
  log_agent_run_started(
      run_id,
      QStringLiteral("validate-batch"),
      report_scope,
      selected_assets.size(),
      selected_style_id_,
      selected_model_id_);
  const auto run_directory = run_artifact_directory(run_id);
  const auto batch_report_path =
      QDir(run_directory).filePath(QStringLiteral("batch-validation-report.json"));
  QVariantList asset_reports;
  QStringList asset_ids;
  QStringList report_warnings;
  auto total_error_count = 0;
  auto total_warning_count = 0;
  QVariantList tool_calls;
  QString cancellation_reason;
  const auto record_cancellation = [&](const QString& reason, const QString& message) {
    if (cancellation_reason.isEmpty()) {
      cancellation_reason = reason;
      errors.append(message);
      append_tool_call(
          &tool_calls,
          QStringLiteral("cancel_run"),
          QStringLiteral("failed"),
          QVariantMap{{QStringLiteral("reason"), reason}});
    }
    return true;
  };
  const auto stop_requested = [&]() {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    if (!cancellation_reason.isEmpty()) {
      return true;
    }
    if (cancellation_requested_) {
      const auto reason = cancellation_reason_.trimmed().isEmpty()
                              ? QStringLiteral("user_cancelled")
                              : cancellation_reason_.trimmed();
      return record_cancellation(
          reason,
          QStringLiteral("Agent run canceled: %1.").arg(reason));
    }
    if (run_timed_out(run_timer, run_timeout_ms_)) {
      return record_cancellation(
          QStringLiteral("timeout"),
          QStringLiteral("Agent run timed out after %1 ms.").arg(run_timeout_ms_));
    }
    return false;
  };

  for (const auto& asset_value : selected_assets) {
    if (stop_requested()) {
      break;
    }
    const auto asset = asset_value.toMap();
    const auto asset_id = asset.value(QStringLiteral("assetId")).toString();
    asset_ids.append(asset_id);
    const auto recipe_json = asset.value(QStringLiteral("recipeJson")).toString();
    const auto recipe_validation = validate_edit_recipe_json(recipe_json);
    if (!recipe_validation.valid) {
      auto asset_report = result_map(false, recipe_validation.errors);
      asset_report.insert(QStringLiteral("assetId"), asset_id);
      asset_report.insert(QStringLiteral("status"), QStringLiteral("failed"));
      asset_reports.append(asset_report);
      total_error_count += recipe_validation.errors.size();
      append_tool_call(
          &tool_calls,
          QStringLiteral("validate_batch_asset"),
          QStringLiteral("failed"),
          QVariantMap{
              {QStringLiteral("assetId"), asset_id},
              {QStringLiteral("errorCount"), recipe_validation.errors.size()},
          });
      continue;
    }

    const auto state = parse_edit_recipe_json(recipe_json);
    QVariantMap asset_report = result_map(true);
    asset_report.insert(QStringLiteral("mode"), QStringLiteral("validate-batch-asset"));
    asset_report.insert(QStringLiteral("runId"), run_id);
    asset_report.insert(QStringLiteral("assetId"), asset_id);
    asset_report.insert(QStringLiteral("assetIds"), QStringList{asset_id});
    asset_report.insert(QStringLiteral("styleId"), selected_style_id_);
    asset_report.insert(QStringLiteral("modelId"), selected_model_id_);
    asset_report.insert(QStringLiteral("modelBackend"), model.value(QStringLiteral("backend")));
    asset_report.insert(QStringLiteral("assetMetadata"), asset);
    asset_report.insert(QStringLiteral("recipeSummary"), recipe_analysis_summary(state, recipe_json));
    asset_report.insert(
        QStringLiteral("analysisSummary"),
        QStringLiteral("Validated the selected photo without changing its recipe."));
    asset_report = render_agent_analysis_artifacts(
        asset.value(QStringLiteral("filePath")).toString(),
        state,
        style,
        run_id,
        asset_id,
        asset_report);
    const auto asset_ok = asset_report.value(QStringLiteral("ok")).toBool();
    const auto asset_warnings = asset_report.value(QStringLiteral("warnings")).toStringList();
    const auto asset_errors = asset_report.value(QStringLiteral("errors")).toStringList();
    asset_report.insert(
        QStringLiteral("status"),
        asset_ok ? QStringLiteral("validated") : QStringLiteral("failed"));
    report_warnings.append(asset_warnings);
    total_warning_count += asset_warnings.size();
    total_error_count += asset_errors.size();
    if (!asset_ok && asset_errors.isEmpty()) {
      ++total_error_count;
    }
    if (!asset_ok) {
      observability::logWarning(
          photoEditorAppLog(),
          QStringLiteral("agent.edit.validation_failed"),
          {{QStringLiteral("run_id"), run_id},
           {QStringLiteral("asset_id"), asset_id},
           {QStringLiteral("mode"), QStringLiteral("validate-batch")},
           {QStringLiteral("warning_count"), asset_warnings.size()},
           {QStringLiteral("error_count"), asset_errors.size()}});
    }
    asset_reports.append(asset_report);
    append_tool_call(
        &tool_calls,
        QStringLiteral("validate_batch_asset"),
        asset_ok ? QStringLiteral("ok") : QStringLiteral("failed"),
        QVariantMap{
            {QStringLiteral("assetId"), asset_id},
            {QStringLiteral("proofPath"), asset_report.value(QStringLiteral("currentProofPath"))},
            {QStringLiteral("warningCount"), asset_warnings.size()},
            {QStringLiteral("errorCount"), asset_errors.size()},
            {QStringLiteral("validationOk"),
             asset_report.value(QStringLiteral("validationReport")).toMap().value(QStringLiteral("ok"))},
        });
  }

  const auto canceled = !cancellation_reason.isEmpty();
  QVariantMap report =
      result_map(!canceled && total_error_count == 0, canceled ? errors : QStringList{}, report_warnings);
  if (canceled) {
    report.insert(QStringLiteral("cancellationReason"), cancellation_reason);
    record_failure_reason(&report, errors.join(QStringLiteral("; ")));
  } else if (total_error_count != 0) {
    record_failure_reason(
        &report,
        QStringLiteral("Batch validation found %1 error%2.")
            .arg(total_error_count)
            .arg(total_error_count == 1 ? QString() : QStringLiteral("s")));
  }
  report.insert(QStringLiteral("mode"), QStringLiteral("validate-batch"));
  report.insert(QStringLiteral("runId"), run_id);
  report.insert(QStringLiteral("assetIds"), asset_ids);
  report.insert(QStringLiteral("scope"), report_scope);
  report.insert(QStringLiteral("styleId"), selected_style_id_);
  report.insert(QStringLiteral("modelId"), selected_model_id_);
  report.insert(QStringLiteral("modelBackend"), model.value(QStringLiteral("backend")));
  report.insert(QStringLiteral("assetReports"), asset_reports);
  report.insert(QStringLiteral("checkedCount"), asset_ids.size());
  report.insert(QStringLiteral("errorCount"), total_error_count);
  report.insert(QStringLiteral("warningCount"), total_warning_count);
  report.insert(QStringLiteral("draftActive"), draftActive());
  report.insert(QStringLiteral("batchConcurrencyLimit"), kAgentBatchConcurrencyLimit);
  report.insert(
      QStringLiteral("batchSummary"),
      batch_summary(
          report.value(QStringLiteral("scope")).toString(),
          asset_ids,
          asset_reports,
          draftActive(),
          false));
  report.insert(QStringLiteral("runArtifactDirectory"), run_directory);
  report.insert(QStringLiteral("batchValidationReportPath"), batch_report_path);
  finalize_timing(&report, started_at_utc, run_timer.elapsed(), tool_calls);
  log_agent_tool_calls(run_id, tool_calls);
  if (!QDir().mkpath(run_directory)) {
    report_warnings.append(QStringLiteral("Unable to create agent run directory: %1").arg(run_directory));
  }
  write_json_artifact(batch_report_path, report, &report_warnings);
  report.insert(QStringLiteral("warnings"), report_warnings);
  write_agent_run_manifest(&report, style, model, selected_model_id_);

  observability::logInfo(
      photoEditorAppLog(),
      QStringLiteral("agent.edit.batch_validated"),
      {{QStringLiteral("run_id"), run_id},
       {QStringLiteral("asset_count"), asset_ids.size()},
       {QStringLiteral("error_count"), total_error_count},
       {QStringLiteral("warning_count"), total_warning_count},
       {QStringLiteral("ok"), report.value(QStringLiteral("ok")).toBool()}});
  if (cancellation_requested_) {
    cancellation_requested_ = false;
    emit cancellationChanged();
  }
  setRunning(false);
  setStatus(canceled ? QStringLiteral("Canceled")
                     : (total_error_count == 0 ? QStringLiteral("Batch validated")
                                               : QStringLiteral("Batch validation failed")));
  if (canceled) {
    setErrorMessage(errors.join(QStringLiteral("; ")));
  } else if (total_error_count != 0) {
    setErrorMessage(QStringLiteral("Batch validation found %1 error%2.")
                        .arg(total_error_count)
                        .arg(total_error_count == 1 ? QString() : QStringLiteral("s")));
  }
  setLastRunReport(report);
  return report;
}

QVariantMap AgentEditController::exportPreflightForSelected(
    const QString& preset,
    const QString& destination_directory,
    const QVariantMap& metrics) {
  return exportPreflightForScope(QStringLiteral("selected-assets"), preset, destination_directory, metrics);
}

QVariantMap AgentEditController::exportPreflightForScope(
    const QString& scope,
    const QString& preset,
    const QString& destination_directory,
    const QVariantMap& metrics) {
  QElapsedTimer run_timer;
  run_timer.start();
  const auto started_at_utc = QDateTime::currentDateTimeUtc();
  if (cancellation_requested_) {
    cancellation_requested_ = false;
    emit cancellationChanged();
  }
  cancellation_reason_.clear();
  setRunning(true);
  setErrorMessage(QString());
  setStatus(QStringLiteral("Running export preflight"));

  QStringList errors;
  if (catalog_model_ == nullptr) {
    errors.append(QStringLiteral("Catalog model is not connected."));
  }
  if (export_job_controller_ == nullptr) {
    errors.append(QStringLiteral("Export job controller is not connected."));
  }

  const auto selected_assets = scoped_asset_maps(catalog_model_, scope);
  const auto report_scope = report_scope_for_assets(scope, selected_assets);
  if (catalog_model_ != nullptr && selected_assets.isEmpty()) {
    errors.append(empty_scope_error(scope, QStringLiteral("export preflight")));
  }

  if (!errors.isEmpty()) {
    setRunning(false);
    setStatus(QStringLiteral("Failed"));
    setErrorMessage(errors.join(QStringLiteral("; ")));
    const auto report = result_map(false, errors);
    setLastRunReport(report);
    return report;
  }

  const auto normalized_preset =
      preset.trimmed().isEmpty() ? QStringLiteral("Full Resolution") : preset.trimmed();
  const auto normalized_destination =
      destination_directory.trimmed().isEmpty() ? export_job_controller_->defaultDestinationDirectory()
                                                : destination_directory.trimmed();
  QVariantList asset_reports;
  QStringList asset_ids;
  auto total_issue_count = 0;
  auto total_error_count = 0;
  auto total_warning_count = 0;
  QVariantList tool_calls;
  QString cancellation_reason;
  const auto record_cancellation = [&](const QString& reason, const QString& message) {
    if (cancellation_reason.isEmpty()) {
      cancellation_reason = reason;
      errors.append(message);
      append_tool_call(
          &tool_calls,
          QStringLiteral("cancel_run"),
          QStringLiteral("failed"),
          QVariantMap{{QStringLiteral("reason"), reason}});
    }
    return true;
  };
  const auto stop_requested = [&]() {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    if (!cancellation_reason.isEmpty()) {
      return true;
    }
    if (cancellation_requested_) {
      const auto reason = cancellation_reason_.trimmed().isEmpty()
                              ? QStringLiteral("user_cancelled")
                              : cancellation_reason_.trimmed();
      return record_cancellation(
          reason,
          QStringLiteral("Agent run canceled: %1.").arg(reason));
    }
    if (run_timed_out(run_timer, run_timeout_ms_)) {
      return record_cancellation(
          QStringLiteral("timeout"),
          QStringLiteral("Agent run timed out after %1 ms.").arg(run_timeout_ms_));
    }
    return false;
  };

  for (const auto& asset_value : selected_assets) {
    if (stop_requested()) {
      break;
    }
    const auto asset = asset_value.toMap();
    const auto asset_id = asset.value(QStringLiteral("assetId")).toString();
    auto preflight = export_job_controller_->exportPreflight(
        asset.value(QStringLiteral("mediaType")).toString(),
        asset.value(QStringLiteral("filePath")).toString(),
        QString(),
        asset.value(QStringLiteral("recipeJson")).toString(),
        normalized_preset,
        normalized_destination,
        metric_int(metrics, QStringLiteral("histogramSampleCount")),
        metric_int(metrics, QStringLiteral("shadowClipCount")),
        metric_int(metrics, QStringLiteral("highlightClipCount")),
        metric_int(metrics, QStringLiteral("rawClipSampleCount")),
        metric_int(metrics, QStringLiteral("rawShadowClipCount")),
        metric_int(metrics, QStringLiteral("rawHighlightClipCount")));
    preflight.insert(QStringLiteral("assetId"), asset_id);
    preflight.insert(QStringLiteral("destinationDirectory"), normalized_destination);
    preflight.insert(QStringLiteral("mediaType"), asset.value(QStringLiteral("mediaType")).toString());
    preflight.insert(QStringLiteral("sourcePath"), asset.value(QStringLiteral("filePath")).toString());
    preflight.insert(
        QStringLiteral("status"),
        preflight.value(QStringLiteral("ok")).toBool() ? QStringLiteral("ready") : QStringLiteral("blocked"));

    asset_reports.append(preflight);
    asset_ids.append(asset_id);
    total_issue_count += preflight.value(QStringLiteral("issueCount")).toInt();
    total_error_count += preflight.value(QStringLiteral("errorCount")).toInt();
    total_warning_count += preflight.value(QStringLiteral("warningCount")).toInt();
    append_tool_call(
        &tool_calls,
        QStringLiteral("export_preflight"),
        preflight.value(QStringLiteral("ok")).toBool() ? QStringLiteral("ok") : QStringLiteral("blocked"),
        QVariantMap{
            {QStringLiteral("assetId"), asset_id},
            {QStringLiteral("preset"), normalized_preset},
            {QStringLiteral("issueCount"), preflight.value(QStringLiteral("issueCount"))},
            {QStringLiteral("errorCount"), preflight.value(QStringLiteral("errorCount"))},
            {QStringLiteral("warningCount"), preflight.value(QStringLiteral("warningCount"))},
        });
  }

  const auto canceled = !cancellation_reason.isEmpty();
  QVariantMap report = result_map(!canceled && total_error_count == 0, canceled ? errors : QStringList{});
  if (canceled) {
    report.insert(QStringLiteral("cancellationReason"), cancellation_reason);
    record_failure_reason(&report, errors.join(QStringLiteral("; ")));
  } else if (total_error_count != 0) {
    record_failure_reason(
        &report,
        QStringLiteral("Export preflight found %1 blocking issue%2.")
            .arg(total_error_count)
            .arg(total_error_count == 1 ? QString() : QStringLiteral("s")));
  }
  const auto run_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const auto model = model_metadata_by_id(available_models_, selected_model_id_);
  log_agent_run_started(
      run_id,
      QStringLiteral("export-preflight"),
      report_scope,
      asset_ids.size(),
      selected_style_id_,
      selected_model_id_);
  QStringList report_warnings;
  const auto run_directory = run_artifact_directory(run_id);
  const auto preflight_report_path =
      QDir(run_directory).filePath(QStringLiteral("preflight-report.json"));
  report.insert(QStringLiteral("mode"), QStringLiteral("export-preflight"));
  report.insert(QStringLiteral("runId"), run_id);
  report.insert(QStringLiteral("assetIds"), asset_ids);
  report.insert(QStringLiteral("scope"), report_scope);
  report.insert(QStringLiteral("styleId"), selected_style_id_);
  report.insert(QStringLiteral("modelId"), selected_model_id_);
  report.insert(QStringLiteral("modelBackend"), model.value(QStringLiteral("backend")));
  report.insert(QStringLiteral("preset"), normalized_preset);
  report.insert(QStringLiteral("destinationDirectory"), normalized_destination);
  report.insert(QStringLiteral("assetReports"), asset_reports);
  report.insert(QStringLiteral("checkedCount"), asset_ids.size());
  report.insert(QStringLiteral("issueCount"), total_issue_count);
  report.insert(QStringLiteral("errorCount"), total_error_count);
  report.insert(QStringLiteral("warningCount"), total_warning_count);
  report.insert(QStringLiteral("draftActive"), draftActive());
  report.insert(QStringLiteral("batchConcurrencyLimit"), kAgentBatchConcurrencyLimit);
  report.insert(
      QStringLiteral("batchSummary"),
      batch_summary(
          report.value(QStringLiteral("scope")).toString(),
          asset_ids,
          asset_reports,
          draftActive(),
          false));
  report.insert(QStringLiteral("runArtifactDirectory"), run_directory);
  report.insert(QStringLiteral("preflightReportPath"), preflight_report_path);
  report.insert(QStringLiteral("warnings"), report_warnings);
  finalize_timing(&report, started_at_utc, run_timer.elapsed(), tool_calls);
  log_agent_tool_calls(run_id, tool_calls);
  if (!QDir().mkpath(run_directory)) {
    report_warnings.append(QStringLiteral("Unable to create agent run directory: %1").arg(run_directory));
  }
  write_json_artifact(preflight_report_path, report, &report_warnings);
  report.insert(QStringLiteral("warnings"), report_warnings);
  write_agent_run_manifest(&report, selectedStyle(), model, selected_model_id_);

  observability::logInfo(
      photoEditorAppLog(),
      QStringLiteral("agent.edit.export_preflight"),
      {{QStringLiteral("run_id"), run_id},
       {QStringLiteral("asset_count"), asset_ids.size()},
       {QStringLiteral("error_count"), total_error_count},
       {QStringLiteral("warning_count"), total_warning_count},
       {QStringLiteral("preset"), normalized_preset}});
  if (cancellation_requested_) {
    cancellation_requested_ = false;
    emit cancellationChanged();
  }
  setRunning(false);
  setStatus(canceled ? QStringLiteral("Canceled")
                     : (total_error_count == 0 ? QStringLiteral("Export preflight ready")
                                               : QStringLiteral("Export preflight failed")));
  if (canceled) {
    setErrorMessage(errors.join(QStringLiteral("; ")));
  } else if (total_error_count != 0) {
    setErrorMessage(QStringLiteral("Export preflight found %1 blocking issue%2.")
                        .arg(total_error_count)
                        .arg(total_error_count == 1 ? QString() : QStringLiteral("s")));
  }
  setLastRunReport(report);
  return report;
}

QVariantMap AgentEditController::queueExportForSelected(
    const QString& preset,
    const QString& destination_directory,
    bool confirmed,
    const QVariantMap& metrics) {
  QElapsedTimer run_timer;
  run_timer.start();
  const auto started_at_utc = QDateTime::currentDateTimeUtc();
  setRunning(true);
  setErrorMessage(QString());
  setStatus(QStringLiteral("Preparing export"));

  QStringList errors;
  if (!confirmed) {
    errors.append(QStringLiteral("Agent export requires explicit confirmation."));
  }
  if (catalog_model_ == nullptr) {
    errors.append(QStringLiteral("Catalog model is not connected."));
  } else if (catalog_model_->selectedAssetCount() < 1) {
    errors.append(QStringLiteral("Agent export requires at least one selected photo."));
  }
  if (export_job_controller_ == nullptr) {
    errors.append(QStringLiteral("Export job controller is not connected."));
  }
  const auto selected_assets = selected_asset_maps(catalog_model_);
  if (catalog_model_ != nullptr && selected_assets.isEmpty()) {
    errors.append(QStringLiteral("No selected photos are available for export."));
  }
  if (!errors.isEmpty()) {
    setRunning(false);
    setStatus(QStringLiteral("Failed"));
    setErrorMessage(errors.join(QStringLiteral("; ")));
    auto report = result_map(false, errors);
    report.insert(QStringLiteral("mode"), QStringLiteral("export-batch"));
    report.insert(QStringLiteral("queuedCount"), 0);
    setLastRunReport(report);
    return report;
  }

  auto preflight = exportPreflightForSelected(preset, destination_directory, metrics);
  if (!preflight.value(QStringLiteral("ok")).toBool()) {
    setRunning(false);
    setStatus(QStringLiteral("Export preflight failed"));
    setErrorMessage(QStringLiteral("Export was not queued because preflight found blocking issues."));
    auto report = result_map(false, {QStringLiteral("Export preflight failed.")});
    report.insert(QStringLiteral("mode"), QStringLiteral("export-batch"));
    report.insert(QStringLiteral("preflightReport"), preflight);
    report.insert(QStringLiteral("queuedCount"), 0);
    setLastRunReport(report);
    return report;
  }

  setRunning(true);
  setStatus(QStringLiteral("Queueing export"));
  const auto normalized_preset = preflight.value(QStringLiteral("preset")).toString();
  const auto normalized_destination = preflight.value(QStringLiteral("destinationDirectory")).toString();
  const auto queued_count = export_job_controller_->queueBatchExport(
      selected_assets,
      normalized_preset,
      normalized_destination);
  const auto expected_count = selected_assets.size();
  const auto ok = queued_count == expected_count;
  QVariantMap report = result_map(ok);
  const auto run_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const auto model = model_metadata_by_id(available_models_, selected_model_id_);
  log_agent_run_started(
      run_id,
      QStringLiteral("export-batch"),
      preflight.value(QStringLiteral("scope")).toString(),
      expected_count,
      selected_style_id_,
      selected_model_id_);
  auto report_warnings = preflight.value(QStringLiteral("warnings")).toStringList();
  auto tool_calls = report_tool_calls(preflight);
  const auto run_directory = run_artifact_directory(run_id);
  const auto export_batch_report_path =
      QDir(run_directory).filePath(QStringLiteral("export-batch-report.json"));
  report.insert(QStringLiteral("mode"), QStringLiteral("export-batch"));
  report.insert(QStringLiteral("runId"), run_id);
  report.insert(QStringLiteral("assetIds"), preflight.value(QStringLiteral("assetIds")));
  report.insert(QStringLiteral("scope"), preflight.value(QStringLiteral("scope")));
  report.insert(QStringLiteral("styleId"), selected_style_id_);
  report.insert(QStringLiteral("modelId"), selected_model_id_);
  report.insert(QStringLiteral("modelBackend"), model.value(QStringLiteral("backend")));
  report.insert(QStringLiteral("preset"), normalized_preset);
  report.insert(QStringLiteral("destinationDirectory"), normalized_destination);
  report.insert(QStringLiteral("preflightReport"), preflight);
  report.insert(QStringLiteral("queuedCount"), queued_count);
  report.insert(QStringLiteral("expectedCount"), expected_count);
  report.insert(QStringLiteral("exportQueueJobCount"), export_job_controller_->jobCount());
  report.insert(QStringLiteral("confirmed"), confirmed);
  report.insert(QStringLiteral("batchConcurrencyLimit"), kAgentBatchConcurrencyLimit);
  report.insert(QStringLiteral("batchSummary"), preflight.value(QStringLiteral("batchSummary")));
  report.insert(QStringLiteral("runArtifactDirectory"), run_directory);
  report.insert(QStringLiteral("exportBatchReportPath"), export_batch_report_path);
  if (!ok) {
    const auto message =
        QStringLiteral("Queued %1 of %2 selected export jobs.").arg(queued_count).arg(expected_count);
    report.insert(QStringLiteral("errors"), QStringList{message});
    record_failure_reason(&report, message);
    setErrorMessage(message);
  }
  append_tool_call(
      &tool_calls,
      QStringLiteral("queue_batch_export"),
      ok ? QStringLiteral("ok") : QStringLiteral("failed"),
      QVariantMap{
          {QStringLiteral("queuedCount"), queued_count},
          {QStringLiteral("expectedCount"), expected_count},
          {QStringLiteral("preset"), normalized_preset},
          {QStringLiteral("confirmed"), confirmed},
      });
  finalize_timing(&report, started_at_utc, run_timer.elapsed(), tool_calls);
  log_agent_tool_calls(run_id, tool_calls);
  report.insert(QStringLiteral("warnings"), report_warnings);
  if (!QDir().mkpath(run_directory)) {
    report_warnings.append(QStringLiteral("Unable to create agent run directory: %1").arg(run_directory));
  }
  write_json_artifact(export_batch_report_path, report, &report_warnings);
  report.insert(QStringLiteral("warnings"), report_warnings);
  write_agent_run_manifest(&report, selectedStyle(), model, selected_model_id_);
  observability::logInfo(
      photoEditorAppLog(),
      QStringLiteral("agent.edit.export_queued"),
      {{QStringLiteral("run_id"), run_id},
       {QStringLiteral("asset_count"), expected_count},
       {QStringLiteral("queued_count"), queued_count},
       {QStringLiteral("preset"), normalized_preset}});
  setRunning(false);
  setStatus(ok ? QStringLiteral("Export queued") : QStringLiteral("Export queue incomplete"));
  setLastRunReport(report);
  return report;
}

QVariantMap AgentEditController::planDraftForSelected(const QString& instructions) const {
  return planDraftForScope(QStringLiteral("selected-assets"), instructions);
}

QVariantMap AgentEditController::planDraftForScope(
    const QString& scope,
    const QString& instructions) const {
  QStringList errors;
  if (catalog_model_ == nullptr) {
    errors.append(QStringLiteral("Catalog model is not connected."));
  }
  const auto style = selectedStyle();
  if (style.isEmpty()) {
    errors.append(QStringLiteral("No valid agent style is selected."));
  }
  const auto scoped_assets = scoped_asset_maps(catalog_model_, scope);
  const auto report_scope = report_scope_for_assets(scope, scoped_assets);
  if (catalog_model_ != nullptr && scoped_assets.isEmpty()) {
    errors.append(empty_scope_error(scope, QStringLiteral("draft planning")));
  }
  if (!errors.isEmpty()) {
    return result_map(false, errors);
  }
  const auto representative_asset = scoped_assets.first().toMap();

  AgentLocalModelPlanRequest request;
  request.model_id = selected_model_id_;
  request.style = style;
  request.asset = representative_asset;
  request.current_recipe_json = representative_asset.value(QStringLiteral("recipeJson")).toString();
  request.user_instructions = instructions.trimmed().isEmpty() ? user_instructions_ : instructions;
  const auto worker_result = plan_agent_recipe_patch_on_worker(request);
  const auto& plan = worker_result.plan;
  if (!plan.ok) {
    return result_map(false, {plan.error_message});
  }
  if (plan.style_constraint_citations.isEmpty()) {
    return result_map(false, {QStringLiteral("The local model did not cite active style constraints.")});
  }

  const auto patch_validation = validate_agent_recipe_patch(plan.recipe_patch, style);
  if (!patch_validation.valid) {
    return result_map(false, patch_validation.errors, patch_validation.warnings);
  }
  const auto recipe_validation = validate_edit_recipe_json(request.current_recipe_json);
  EditRecipeState current_state;
  if (recipe_validation.valid) {
    current_state = parse_edit_recipe_json(request.current_recipe_json);
  }

  QVariantMap report = result_map(true, {}, plan.warnings + patch_validation.warnings);
  report.insert(QStringLiteral("modelId"), plan.model_id);
  report.insert(QStringLiteral("styleId"), selected_style_id_);
  report.insert(QStringLiteral("assetId"), representative_asset.value(QStringLiteral("assetId")));
  QStringList asset_ids;
  for (const auto& asset_value : scoped_assets) {
    asset_ids.append(asset_value.toMap().value(QStringLiteral("assetId")).toString());
  }
  report.insert(QStringLiteral("assetIds"), asset_ids);
  report.insert(QStringLiteral("representativeAssetId"), representative_asset.value(QStringLiteral("assetId")));
  report.insert(QStringLiteral("scope"), report_scope);
  report.insert(QStringLiteral("planSummary"), plan.plan_summary);
  report.insert(
      QStringLiteral("editGoals"),
      agent_edit_goals(style, request.user_instructions, plan.recipe_patch));
  if (recipe_validation.valid) {
    report.insert(
        QStringLiteral("recipeSummary"),
        recipe_analysis_summary(current_state, request.current_recipe_json));
    report.insert(
        QStringLiteral("riskAssessment"),
        agent_risk_assessment(
            current_state,
            plan.warnings + patch_validation.warnings,
            plan.recipe_patch));
  }
  report.insert(QStringLiteral("styleConstraintCitations"), plan.style_constraint_citations);
  report.insert(QStringLiteral("recipePatch"), plan.recipe_patch);
  report.insert(QStringLiteral("recipeBaseMode"), recipe_base_mode_);
  report.insert(QStringLiteral("manualEditPolicy"), manual_edit_policy_for_base_mode(recipe_base_mode_));
  report.insert(QStringLiteral("modelRunnerMode"), QStringLiteral("worker-thread"));
  report.insert(QStringLiteral("uiEventPumpCount"), worker_result.ui_event_pump_count);
  return report;
}

QVariantMap AgentEditController::runDraftOnSelected(const QString& instructions) {
  return runDraftForScope(QStringLiteral("selected-assets"), instructions);
}

QVariantMap AgentEditController::runDraftForScope(
    const QString& scope,
    const QString& instructions) {
  if (manual_editing_active_) {
    auto report = result_map(
        false,
        {QStringLiteral("Agent work is paused while manual editing is active.")});
    report.insert(QStringLiteral("mode"), QStringLiteral("draft"));
    report.insert(QStringLiteral("scope"), normalized_agent_scope(scope));
    report.insert(QStringLiteral("backgroundWorkPolicy"), background_work_policy_report(true));
    report.insert(QStringLiteral("draftActive"), draftActive());
    setStatus(QStringLiteral("Paused for manual edit"));
    setErrorMessage(report.value(QStringLiteral("failureReason")).toString());
    setLastRunReport(report);
    return report;
  }
  QElapsedTimer run_timer;
  run_timer.start();
  const auto started_at_utc = QDateTime::currentDateTimeUtc();
  setRunning(true);
  setErrorMessage(QString());
  setStatus(QStringLiteral("Planning draft"));
  rollback_on_cancel_ = false;
  QElapsedTimer plan_timer;
  plan_timer.start();
  auto plan = planDraftForScope(scope, instructions);
  if (!plan.value(QStringLiteral("ok")).toBool()) {
    const auto errors = plan.value(QStringLiteral("errors")).toStringList();
    setRunning(false);
    setStatus(QStringLiteral("Failed"));
    setErrorMessage(errors.join(QStringLiteral("; ")));
    setLastRunReport(plan);
    return plan;
  }
  if (cancellation_requested_) {
    const auto cancellation_reason = cancellation_reason_.trimmed().isEmpty()
                                         ? QStringLiteral("user_cancelled")
                                         : cancellation_reason_.trimmed();
    if (draft_run_id_.isEmpty()) {
      draft_run_id_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    const auto selected_assets = scoped_asset_maps(catalog_model_, scope);
    const auto report_scope = report_scope_for_assets(scope, selected_assets);
    const auto target_asset_ids = asset_ids_from_maps(selected_assets);
    QVariantMap canceled = result_map(
        false,
        {QStringLiteral("Agent run canceled: %1.").arg(cancellation_reason)});
    const auto model =
        model_metadata_by_id(available_models_, plan.value(QStringLiteral("modelId")).toString());
    const auto plan_tool_call = tool_call_record(
        QStringLiteral("plan_agent_recipe_patch"),
        QStringLiteral("canceled"),
        QVariantMap{
            {QStringLiteral("modelId"), plan.value(QStringLiteral("modelId"))},
            {QStringLiteral("styleId"), selected_style_id_},
            {QStringLiteral("runnerMode"), plan.value(QStringLiteral("modelRunnerMode"))},
            {QStringLiteral("uiEventPumpCount"), plan.value(QStringLiteral("uiEventPumpCount"))},
            {QStringLiteral("durationMs"), plan_timer.elapsed()},
        });
    const QVariantList tool_calls{plan_tool_call};
    canceled.insert(QStringLiteral("mode"), QStringLiteral("draft"));
    canceled.insert(QStringLiteral("runId"), draft_run_id_);
    canceled.insert(QStringLiteral("scope"), report_scope);
    canceled.insert(QStringLiteral("assetIds"), target_asset_ids);
    canceled.insert(QStringLiteral("cancellationReason"), cancellation_reason);
    canceled.insert(QStringLiteral("styleId"), selected_style_id_);
    canceled.insert(QStringLiteral("modelId"), plan.value(QStringLiteral("modelId")));
    canceled.insert(QStringLiteral("modelBackend"), model.value(QStringLiteral("backend")));
    insert_cancel_rollback_metadata(
        &canceled,
        draft_run_id_,
        report_scope,
        target_asset_ids,
        rollback_on_cancel_,
        0,
        0);
    finalize_timing(&canceled, started_at_utc, run_timer.elapsed(), tool_calls);
    log_agent_tool_calls(draft_run_id_, tool_calls);
    write_agent_run_manifest(
        &canceled,
        selectedStyle(),
        model,
        plan.value(QStringLiteral("modelId")).toString());
    cancellation_requested_ = false;
    rollback_on_cancel_ = false;
    emit cancellationChanged();
    setRunning(false);
    setStatus(QStringLiteral("Canceled"));
    setErrorMessage(canceled.value(QStringLiteral("failureReason")).toString());
    setLastRunReport(canceled);
    return canceled;
  }

  const auto scoped_assets = scoped_asset_maps(catalog_model_, scope);
  auto batch_plan_metadata = batch_cluster_metadata(scoped_assets);
  QHash<QString, QVariantMap> patches_by_asset_id;
  QVariantList cluster_plans;
  QVariantList style_constraint_citations = plan.value(QStringLiteral("styleConstraintCitations")).toList();
  QStringList plan_warnings = plan.value(QStringLiteral("warnings")).toStringList();
  if (scoped_assets.size() > 1) {
    const auto style = selectedStyle();
    const auto plan_instructions =
        instructions.trimmed().isEmpty() ? user_instructions_ : instructions;
    const auto clusters = batch_plan_metadata.value(QStringLiteral("batchClusters")).toList();
    QVariantMap first_cluster_plan;
    QStringList planning_errors;
    for (const auto& cluster_value : clusters) {
      if (cancellation_requested_) {
        break;
      }
      const auto cluster = cluster_value.toMap();
      const auto representative_asset_id =
          cluster.value(QStringLiteral("representativeAssetId")).toString();
      QVariantMap representative_asset;
      for (const auto& asset_value : scoped_assets) {
        const auto asset = asset_value.toMap();
        if (asset.value(QStringLiteral("assetId")).toString() == representative_asset_id) {
          representative_asset = asset;
          break;
        }
      }
      if (representative_asset.isEmpty()) {
        planning_errors.append(
            QStringLiteral("%1: representative asset is unavailable.")
                .arg(cluster.value(QStringLiteral("clusterId")).toString()));
        continue;
      }

      AgentLocalModelPlanRequest request;
      request.model_id = selected_model_id_;
      request.style = style;
      request.asset = representative_asset;
      request.current_recipe_json = representative_asset.value(QStringLiteral("recipeJson")).toString();
      request.user_instructions = plan_instructions;
      const auto worker_result = plan_agent_recipe_patch_on_worker(request);
      if (cancellation_requested_) {
        break;
      }
      const auto& cluster_plan = worker_result.plan;
      if (!cluster_plan.ok) {
        planning_errors.append(cluster_plan.error_message);
        continue;
      }
      if (cluster_plan.style_constraint_citations.isEmpty()) {
        planning_errors.append(
            QStringLiteral("%1: the local model did not cite active style constraints.")
                .arg(cluster.value(QStringLiteral("clusterId")).toString()));
        continue;
      }
      const auto patch_validation = validate_agent_recipe_patch(cluster_plan.recipe_patch, style);
      if (!patch_validation.valid) {
        planning_errors.append(patch_validation.errors);
        plan_warnings.append(patch_validation.warnings);
        continue;
      }

      QVariantMap cluster_plan_report;
      cluster_plan_report.insert(QStringLiteral("clusterId"), cluster.value(QStringLiteral("clusterId")));
      cluster_plan_report.insert(QStringLiteral("clusterKey"), cluster.value(QStringLiteral("clusterKey")));
      cluster_plan_report.insert(QStringLiteral("assetIds"), cluster.value(QStringLiteral("assetIds")));
      cluster_plan_report.insert(QStringLiteral("assetCount"), cluster.value(QStringLiteral("assetCount")));
      cluster_plan_report.insert(QStringLiteral("representativeAssetId"), representative_asset_id);
      cluster_plan_report.insert(QStringLiteral("representativeReason"), cluster.value(QStringLiteral("representativeReason")));
      cluster_plan_report.insert(QStringLiteral("syncGroups"), cluster.value(QStringLiteral("syncGroups")));
      cluster_plan_report.insert(QStringLiteral("availableSignals"), cluster.value(QStringLiteral("availableSignals")));
      cluster_plan_report.insert(QStringLiteral("unavailableSignals"), cluster.value(QStringLiteral("unavailableSignals")));
      cluster_plan_report.insert(QStringLiteral("modelId"), cluster_plan.model_id);
      cluster_plan_report.insert(QStringLiteral("planSummary"), cluster_plan.plan_summary);
      cluster_plan_report.insert(QStringLiteral("recipePatch"), cluster_plan.recipe_patch);
      cluster_plan_report.insert(QStringLiteral("styleConstraintCitations"), cluster_plan.style_constraint_citations);
      cluster_plan_report.insert(QStringLiteral("uiEventPumpCount"), worker_result.ui_event_pump_count);
      cluster_plans.append(cluster_plan_report);
      if (first_cluster_plan.isEmpty()) {
        first_cluster_plan = cluster_plan_report;
        style_constraint_citations = cluster_plan.style_constraint_citations;
      }
      for (const auto& asset_id : cluster.value(QStringLiteral("assetIds")).toStringList()) {
        patches_by_asset_id.insert(asset_id, cluster_plan.recipe_patch);
      }
      plan_warnings.append(cluster_plan.warnings + patch_validation.warnings);
    }
    if (!planning_errors.isEmpty()) {
      auto failed_plan = result_map(false, planning_errors, plan_warnings);
      setRunning(false);
      setStatus(QStringLiteral("Failed"));
      setErrorMessage(planning_errors.join(QStringLiteral("; ")));
      setLastRunReport(failed_plan);
      return failed_plan;
    }
    if (!first_cluster_plan.isEmpty()) {
      plan.insert(QStringLiteral("assetId"), first_cluster_plan.value(QStringLiteral("representativeAssetId")));
      plan.insert(QStringLiteral("representativeAssetId"), first_cluster_plan.value(QStringLiteral("representativeAssetId")));
      plan.insert(QStringLiteral("representativeAssetIds"), batch_plan_metadata.value(QStringLiteral("representativeAssetIds")));
      plan.insert(
          QStringLiteral("planSummary"),
          QStringLiteral("Clustered batch draft with %1 representative plan%2.")
              .arg(cluster_plans.size())
              .arg(cluster_plans.size() == 1 ? QString() : QStringLiteral("s")));
      plan.insert(QStringLiteral("recipePatch"), first_cluster_plan.value(QStringLiteral("recipePatch")));
      plan.insert(QStringLiteral("styleConstraintCitations"), style_constraint_citations);
      plan.insert(QStringLiteral("clusterPlanning"), cluster_plans);
      plan.insert(QStringLiteral("warnings"), plan_warnings);
      for (auto iterator = batch_plan_metadata.cbegin(); iterator != batch_plan_metadata.cend(); ++iterator) {
        plan.insert(iterator.key(), iterator.value());
      }
    }
  } else {
    for (auto iterator = batch_plan_metadata.cbegin(); iterator != batch_plan_metadata.cend(); ++iterator) {
      plan.insert(iterator.key(), iterator.value());
    }
  }
  if (cancellation_requested_) {
    const auto cancellation_reason = cancellation_reason_.trimmed().isEmpty()
                                         ? QStringLiteral("user_cancelled")
                                         : cancellation_reason_.trimmed();
    if (draft_run_id_.isEmpty()) {
      draft_run_id_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    const auto report_scope = report_scope_for_assets(scope, scoped_assets);
    const auto target_asset_ids = asset_ids_from_maps(scoped_assets);
    QVariantMap canceled = result_map(
        false,
        {QStringLiteral("Agent run canceled: %1.").arg(cancellation_reason)},
        plan_warnings);
    const auto model =
        model_metadata_by_id(available_models_, plan.value(QStringLiteral("modelId")).toString());
    canceled.insert(QStringLiteral("mode"), QStringLiteral("draft"));
    canceled.insert(QStringLiteral("runId"), draft_run_id_);
    canceled.insert(QStringLiteral("scope"), report_scope);
    canceled.insert(QStringLiteral("assetIds"), target_asset_ids);
    canceled.insert(QStringLiteral("cancellationReason"), cancellation_reason);
    canceled.insert(QStringLiteral("styleId"), selected_style_id_);
    canceled.insert(QStringLiteral("modelId"), plan.value(QStringLiteral("modelId")));
    canceled.insert(QStringLiteral("modelBackend"), model.value(QStringLiteral("backend")));
    canceled.insert(QStringLiteral("clusterPlanning"), cluster_plans);
    canceled.insert(QStringLiteral("batchClusters"), batch_plan_metadata.value(QStringLiteral("batchClusters")));
    canceled.insert(QStringLiteral("representativeAssetIds"), batch_plan_metadata.value(QStringLiteral("representativeAssetIds")));
    canceled.insert(
        QStringLiteral("batchSummary"),
        batch_summary(report_scope, target_asset_ids, {}, false, false));
    insert_cancel_rollback_metadata(
        &canceled,
        draft_run_id_,
        report_scope,
        target_asset_ids,
        rollback_on_cancel_,
        0,
        0);
    finalize_timing(&canceled, started_at_utc, run_timer.elapsed(), {});
    write_agent_run_manifest(
        &canceled,
        selectedStyle(),
        model,
        plan.value(QStringLiteral("modelId")).toString());
    cancellation_requested_ = false;
    rollback_on_cancel_ = false;
    emit cancellationChanged();
    setRunning(false);
    setStatus(QStringLiteral("Canceled"));
    setErrorMessage(canceled.value(QStringLiteral("failureReason")).toString());
    setLastRunReport(canceled);
    return canceled;
  }

  observability::logInfo(
      photoEditorAppLog(),
      QStringLiteral("agent.edit.run_planned"),
      {{QStringLiteral("asset_id"), plan.value(QStringLiteral("assetId")).toString()},
       {QStringLiteral("style_id"), selected_style_id_},
       {QStringLiteral("model_id"), plan.value(QStringLiteral("modelId")).toString()}});
  batch_plan_metadata.insert(QStringLiteral("clusterPlanning"), cluster_plans);
  auto draft = applyDraftRecipePatchesForScope(
      scope,
      plan.value(QStringLiteral("recipePatch")).toMap(),
      patches_by_asset_id,
      batch_plan_metadata);
  const auto model = model_metadata_by_id(available_models_, plan.value(QStringLiteral("modelId")).toString());
  auto tool_calls = report_tool_calls(draft);
  const auto plan_tool_call = tool_call_record(
      QStringLiteral("plan_agent_recipe_patch"),
      plan.value(QStringLiteral("ok")).toBool() ? QStringLiteral("ok") : QStringLiteral("failed"),
      QVariantMap{
          {QStringLiteral("modelId"), plan.value(QStringLiteral("modelId"))},
          {QStringLiteral("styleId"), selected_style_id_},
          {QStringLiteral("runnerMode"), plan.value(QStringLiteral("modelRunnerMode"))},
          {QStringLiteral("uiEventPumpCount"), plan.value(QStringLiteral("uiEventPumpCount"))},
          {QStringLiteral("durationMs"), plan_timer.elapsed()},
          {QStringLiteral("patchGroupCount"), plan.value(QStringLiteral("recipePatch")).toMap().size()},
          {QStringLiteral("clusterCount"), plan.value(QStringLiteral("clusterCount"))},
          {QStringLiteral("representativeAssetIds"), plan.value(QStringLiteral("representativeAssetIds"))},
          {QStringLiteral("citationCount"),
           plan.value(QStringLiteral("styleConstraintCitations")).toList().size()},
      });
  tool_calls.prepend(plan_tool_call);
  log_agent_tool_calls(draft.value(QStringLiteral("runId")).toString(), QVariantList{plan_tool_call});
  draft.insert(QStringLiteral("mode"), QStringLiteral("draft"));
  draft.insert(QStringLiteral("modelId"), plan.value(QStringLiteral("modelId")));
  draft.insert(QStringLiteral("modelBackend"), model.value(QStringLiteral("backend")));
  draft.insert(QStringLiteral("planSummary"), plan.value(QStringLiteral("planSummary")));
  draft.insert(QStringLiteral("editGoals"), plan.value(QStringLiteral("editGoals")));
  draft.insert(QStringLiteral("riskAssessment"), plan.value(QStringLiteral("riskAssessment")));
  draft.insert(QStringLiteral("recipeBaseMode"), plan.value(QStringLiteral("recipeBaseMode")));
  draft.insert(QStringLiteral("manualEditPolicy"), plan.value(QStringLiteral("manualEditPolicy")));
  draft.insert(QStringLiteral("modelRunnerMode"), plan.value(QStringLiteral("modelRunnerMode")));
  draft.insert(QStringLiteral("uiEventPumpCount"), plan.value(QStringLiteral("uiEventPumpCount")));
  draft.insert(
      QStringLiteral("styleConstraintCitations"),
      plan.value(QStringLiteral("styleConstraintCitations")));
  draft.insert(QStringLiteral("plannedRecipePatch"), plan.value(QStringLiteral("recipePatch")));
  mark_agent_draft_review_required(&draft);
  finalize_timing(&draft, started_at_utc, run_timer.elapsed(), tool_calls);
  write_agent_run_manifest(&draft, selectedStyle(), model, plan.value(QStringLiteral("modelId")).toString());
  setLastRunReport(draft);
  return draft;
}

QVariantMap AgentEditController::applyDraftRecipePatchToSelected(const QVariantMap& patch) {
  return applyDraftRecipePatchForScope(QStringLiteral("selected-assets"), patch);
}

QVariantMap AgentEditController::applyDraftRecipePatchForScope(
    const QString& scope,
    const QVariantMap& patch) {
  return applyDraftRecipePatchesForScope(scope, patch, {}, {});
}

QVariantMap AgentEditController::applyDraftRecipePatchesForScope(
    const QString& scope,
    const QVariantMap& patch,
    const QHash<QString, QVariantMap>& patches_by_asset_id,
    const QVariantMap& batch_plan_metadata) {
  QElapsedTimer run_timer;
  run_timer.start();
  const auto started_at_utc = QDateTime::currentDateTimeUtc();
  if (cancellation_requested_) {
    cancellation_requested_ = false;
    emit cancellationChanged();
  }
  cancellation_reason_.clear();
  setRunning(true);
  setErrorMessage(QString());
  setStatus(QStringLiteral("Applying draft"));

  QStringList errors;
  if (catalog_model_ == nullptr) {
    errors.append(QStringLiteral("Catalog model is not connected."));
  }
  const auto validation = validateRecipePatch(patch);
  if (!validation.value(QStringLiteral("ok")).toBool()) {
    errors.append(validation.value(QStringLiteral("errors")).toStringList());
  }
  QStringList patch_validation_warnings = validation.value(QStringLiteral("warnings")).toStringList();
  QSet<QString> validated_patch_keys;
  validated_patch_keys.insert(QString::fromUtf8(QJsonDocument::fromVariant(patch).toJson(QJsonDocument::Compact)));
  for (auto iterator = patches_by_asset_id.cbegin(); iterator != patches_by_asset_id.cend(); ++iterator) {
    const auto patch_key =
        QString::fromUtf8(QJsonDocument::fromVariant(iterator.value()).toJson(QJsonDocument::Compact));
    if (validated_patch_keys.contains(patch_key)) {
      continue;
    }
    validated_patch_keys.insert(patch_key);
    const auto per_asset_validation = validateRecipePatch(iterator.value());
    if (!per_asset_validation.value(QStringLiteral("ok")).toBool()) {
      errors.append(per_asset_validation.value(QStringLiteral("errors")).toStringList());
    }
    patch_validation_warnings.append(per_asset_validation.value(QStringLiteral("warnings")).toStringList());
  }
  const auto selected_assets = scoped_asset_maps(catalog_model_, scope);
  const auto report_scope = report_scope_for_assets(scope, selected_assets);
  if (catalog_model_ != nullptr && selected_assets.isEmpty()) {
    errors.append(empty_scope_error(scope, QStringLiteral("agent draft editing")));
  }
  const auto style = selectedStyle();
  if (selected_assets.size() > 1) {
    errors.append(selected_set_global_edit_errors(patch, style));
    for (const auto& per_asset_patch : patches_by_asset_id) {
      errors.append(selected_set_global_edit_errors(per_asset_patch, style));
    }
  }
  if (!errors.isEmpty()) {
    setRunning(false);
    setStatus(QStringLiteral("Failed"));
    setErrorMessage(errors.join(QStringLiteral("; ")));
    const auto report = result_map(false, errors);
    setLastRunReport(report);
    return report;
  }

  if (draft_run_id_.isEmpty()) {
    draft_run_id_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
  }
  log_agent_run_started(
      draft_run_id_,
      QStringLiteral("draft-apply"),
      report_scope,
      selected_assets.size(),
      selected_style_id_,
      selected_model_id_);

  QHash<QString, QString> original_recipes;
  QHash<QString, QString> next_recipes;
  QVariantList asset_reports;
  QStringList asset_ids;
  QVariantMap first_final_patch;
  QVariantMap first_risk_assessment;
  auto report_warnings = patch_validation_warnings;
  const auto base_mode = normalized_recipe_base_mode(recipe_base_mode_);
  const auto manual_edit_policy = manual_edit_policy_for_base_mode(base_mode);
  auto batch_clusters = batch_plan_metadata.value(QStringLiteral("batchClusters")).toList();
  const auto asset_cluster_assignments =
      batch_plan_metadata.value(QStringLiteral("assetClusterAssignments")).toMap();
  QVariantList tool_calls;
  QString cancellation_reason;
  const auto record_cancellation = [&](const QString& reason, const QString& message) {
    if (cancellation_reason.isEmpty()) {
      cancellation_reason = reason;
      errors.append(message);
    }
    return true;
  };
  const auto stop_requested = [&]() {
    if (QCoreApplication::instance() != nullptr) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    }
    if (!cancellation_reason.isEmpty()) {
      return true;
    }
    if (cancellation_requested_) {
      const auto reason = cancellation_reason_.trimmed().isEmpty()
                              ? QStringLiteral("user_cancelled")
                              : cancellation_reason_.trimmed();
      return record_cancellation(
          reason,
          QStringLiteral("Agent run canceled: %1.").arg(reason));
    }
    if (run_timed_out(run_timer, run_timeout_ms_)) {
      return record_cancellation(
          QStringLiteral("timeout"),
          QStringLiteral("Agent run timed out after %1 ms.").arg(run_timeout_ms_));
    }
    return false;
  };
  append_tool_call(
      &tool_calls,
      QStringLiteral("validate_recipe_patch"),
      validation.value(QStringLiteral("ok")).toBool() ? QStringLiteral("ok") : QStringLiteral("failed"),
      QVariantMap{
          {QStringLiteral("patchGroupCount"), patch.size()},
          {QStringLiteral("perAssetPatchCount"), patches_by_asset_id.size()},
          {QStringLiteral("warningCount"), report_warnings.size()},
      });
  for (const auto& asset_value : selected_assets) {
    const auto asset = asset_value.toMap();
    const auto asset_id = asset.value(QStringLiteral("assetId")).toString();
    const auto requested_patch = patches_by_asset_id.value(asset_id, patch);
    const auto original_recipe_json = asset.value(QStringLiteral("recipeJson")).toString();
    const auto recipe_validation = validate_edit_recipe_json(original_recipe_json);
    if (!recipe_validation.valid) {
      errors.append(
          QStringLiteral("%1: %2")
              .arg(asset_id, recipe_validation.errors.join(QStringLiteral("; "))));
      continue;
    }

    const auto original_state = parse_edit_recipe_json(original_recipe_json);
    auto final_patch = requested_patch;
    auto final_state = original_state;
    QString next_recipe_json;
    QVariantMap image_validation;
    QVariantList validation_iterations;
    auto revision_count = 0;
    auto stopped_at_iteration_limit = false;
    for (auto iteration = 0; iteration <= kMaxValidationRevisionIterations; ++iteration) {
      if (stop_requested()) {
        break;
      }
      auto state = append_named_recipe_snapshot(
          original_state,
          QStringLiteral("Agent draft %1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)),
          QStringLiteral("agent-%1").arg(draft_run_id_.left(8)));
      state = agent_base_state_for_mode(original_state, state, base_mode);
      tag_latest_agent_snapshot(
          &state,
          draft_run_id_,
          selected_model_id_,
          selected_style_id_,
          QDateTime::currentDateTimeUtc());
      apply_agent_recipe_patch(&state, final_patch);

      auto candidate_recipe_json = serialize_edit_recipe_json(state);
      const auto next_recipe_validation = validate_edit_recipe_json(candidate_recipe_json);
      if (!next_recipe_validation.valid) {
        errors.append(
            QStringLiteral("%1: %2")
                .arg(asset_id, next_recipe_validation.errors.join(QStringLiteral("; "))));
        observability::logWarning(
            photoEditorAppLog(),
            QStringLiteral("agent.edit.validation_failed"),
            {{QStringLiteral("run_id"), draft_run_id_},
             {QStringLiteral("asset_id"), asset_id},
             {QStringLiteral("mode"), QStringLiteral("draft-apply")},
             {QStringLiteral("stage"), QStringLiteral("recipe-json")},
             {QStringLiteral("error_count"), next_recipe_validation.errors.size()}});
        break;
      }

      image_validation = render_agent_proof_artifacts(
          asset.value(QStringLiteral("filePath")).toString(),
          original_state,
          state,
          style,
          draft_run_id_,
          asset_id,
          final_patch,
          stop_requested);
      const auto iteration_warnings =
          image_validation.value(QStringLiteral("warnings")).toStringList();
      validation_iterations.append(QVariantMap{
          {QStringLiteral("iteration"), iteration},
          {QStringLiteral("ok"), image_validation.value(QStringLiteral("ok"))},
          {QStringLiteral("warningCount"), iteration_warnings.size()},
          {QStringLiteral("warnings"), iteration_warnings},
          {QStringLiteral("recipePatch"), final_patch},
      });
      append_tool_call(
          &tool_calls,
          QStringLiteral("render_proof_and_validate"),
          image_validation.value(QStringLiteral("ok")).toBool() ? QStringLiteral("ok") : QStringLiteral("failed"),
          QVariantMap{
              {QStringLiteral("assetId"), asset_id},
              {QStringLiteral("recipeBaseMode"), base_mode},
              {QStringLiteral("iteration"), iteration},
              {QStringLiteral("proofWidth"), image_validation.value(QStringLiteral("proofWidth"))},
              {QStringLiteral("proofHeight"), image_validation.value(QStringLiteral("proofHeight"))},
              {QStringLiteral("warningCount"), iteration_warnings.size()},
          });
      if (!image_validation.value(QStringLiteral("ok")).toBool()) {
        observability::logWarning(
            photoEditorAppLog(),
            QStringLiteral("agent.edit.validation_failed"),
            {{QStringLiteral("run_id"), draft_run_id_},
             {QStringLiteral("asset_id"), asset_id},
             {QStringLiteral("mode"), QStringLiteral("draft-apply")},
             {QStringLiteral("stage"), QStringLiteral("render-proof")},
             {QStringLiteral("iteration"), iteration},
             {QStringLiteral("warning_count"), iteration_warnings.size()}});
      }
      final_state = state;
      annotate_latest_agent_snapshot_diff(
          &final_state,
          final_patch,
          image_validation.value(QStringLiteral("recipeDiff")).toMap());
      candidate_recipe_json = serialize_edit_recipe_json(final_state);
      const auto annotated_recipe_validation = validate_edit_recipe_json(candidate_recipe_json);
      if (!annotated_recipe_validation.valid) {
        errors.append(
            QStringLiteral("%1: %2")
                .arg(asset_id, annotated_recipe_validation.errors.join(QStringLiteral("; "))));
        break;
      }
      next_recipe_json = candidate_recipe_json;
      if (!image_validation.value(QStringLiteral("ok")).toBool() ||
          iteration_warnings.isEmpty() ||
          iteration == kMaxValidationRevisionIterations) {
        stopped_at_iteration_limit = !iteration_warnings.isEmpty() &&
                                    iteration == kMaxValidationRevisionIterations;
        break;
      }
      const auto revised_patch = revise_recipe_patch_from_validation(final_patch, image_validation);
      if (revised_patch == final_patch) {
        break;
      }
      final_patch = revised_patch;
      ++revision_count;
      append_tool_call(
          &tool_calls,
          QStringLiteral("revise_recipe_patch_from_validation"),
          QStringLiteral("ok"),
          QVariantMap{
              {QStringLiteral("assetId"), asset_id},
              {QStringLiteral("iteration"), iteration},
              {QStringLiteral("warningCount"), iteration_warnings.size()},
          });
    }
    if (!errors.isEmpty()) {
      continue;
    }
    if (!image_validation.value(QStringLiteral("ok")).toBool()) {
      errors.append(
          QStringLiteral("%1: validation failed before draft apply").arg(asset_id));
      continue;
    }
    if (next_recipe_json.isEmpty()) {
      errors.append(QStringLiteral("%1: no validated draft recipe was produced").arg(asset_id));
      continue;
    }

    report_warnings.append(image_validation.value(QStringLiteral("warnings")).toStringList());
    if (first_final_patch.isEmpty()) {
      first_final_patch = final_patch;
      first_risk_assessment = agent_risk_assessment(
          original_state,
          image_validation.value(QStringLiteral("warnings")).toStringList(),
          final_patch);
    }

    QVariantMap asset_report;
    asset_report.insert(QStringLiteral("assetId"), asset_id);
    asset_report.insert(QStringLiteral("virtualCopy"), asset.value(QStringLiteral("virtualCopy")).toBool());
    asset_report.insert(QStringLiteral("sourceAssetId"), asset.value(QStringLiteral("sourceAssetId")).toString());
    asset_report.insert(QStringLiteral("variantName"), asset.value(QStringLiteral("variantName")).toString());
    asset_report.insert(QStringLiteral("variantIndex"), asset.value(QStringLiteral("variantIndex")).toInt());
    asset_report.insert(
        QStringLiteral("variantFamilyId"),
        asset.value(QStringLiteral("sourceAssetId")).toString().trimmed().isEmpty()
            ? asset_id
            : asset.value(QStringLiteral("sourceAssetId")).toString().trimmed());
    asset_report.insert(
        QStringLiteral("virtualCopyPolicy"),
        QStringLiteral("target-asset-id-only"));
    asset_report.insert(QStringLiteral("status"), QStringLiteral("prepared"));
    asset_report.insert(QStringLiteral("recipeBaseMode"), base_mode);
    asset_report.insert(QStringLiteral("manualEditPolicy"), manual_edit_policy);
    asset_report.insert(
        QStringLiteral("riskAssessment"),
        agent_risk_assessment(
            original_state,
            image_validation.value(QStringLiteral("warnings")).toStringList(),
            final_patch));
    asset_report.insert(QStringLiteral("revisionCount"), revision_count);
    asset_report.insert(QStringLiteral("validationIterationLimit"), kMaxValidationRevisionIterations);
    asset_report.insert(QStringLiteral("validationIterationLimitReached"), stopped_at_iteration_limit);
    asset_report.insert(QStringLiteral("validationIterations"), validation_iterations);
    asset_report.insert(QStringLiteral("usesDraftPreviewsForPlanning"), true);
    asset_report.insert(QStringLiteral("draftPreviewPolicy"), QStringLiteral("render-each-validation-iteration"));
    asset_report.insert(
        QStringLiteral("draftPreviewCount"),
        static_cast<int>(validation_iterations.size()));
    asset_report.insert(QStringLiteral("usesSettledPreviewForFinalProof"), true);
    asset_report.insert(QStringLiteral("settledPreviewPolicy"), QStringLiteral("final-validated-recipe-proof"));
    asset_report.insert(
        QStringLiteral("settledPreviewReady"),
        image_validation.value(QStringLiteral("ok")).toBool() &&
            !image_validation.value(QStringLiteral("artifacts"))
                 .toMap()
                 .value(QStringLiteral("afterProofPath"))
                 .toString()
                 .isEmpty());
    const auto cluster_assignment = asset_cluster_assignments.value(asset_id).toMap();
    if (!cluster_assignment.isEmpty()) {
      asset_report.insert(QStringLiteral("clusterId"), cluster_assignment.value(QStringLiteral("clusterId")));
      asset_report.insert(QStringLiteral("clusterKey"), cluster_assignment.value(QStringLiteral("clusterKey")));
      asset_report.insert(
          QStringLiteral("clusterRepresentativeAssetId"),
          cluster_assignment.value(QStringLiteral("clusterRepresentativeAssetId")));
      asset_report.insert(QStringLiteral("clusterSyncGroups"), cluster_assignment.value(QStringLiteral("clusterSyncGroups")));
      asset_report.insert(
          QStringLiteral("clusterOutlierCorrected"),
          revision_count > 0 || stopped_at_iteration_limit);
    }
    asset_report.insert(QStringLiteral("finalRecipePatch"), final_patch);
    asset_report.insert(QStringLiteral("validationReport"), image_validation);
    const auto mask_local_proof = mask_local_proof_report(final_patch, image_validation);
    if (mask_local_proof.value(QStringLiteral("required")).toBool()) {
      asset_report.insert(QStringLiteral("maskLocalProof"), mask_local_proof);
    }
    asset_report.insert(QStringLiteral("artifacts"), image_validation.value(QStringLiteral("artifacts")).toMap());
    asset_report.insert(QStringLiteral("recipeDiff"), image_validation.value(QStringLiteral("recipeDiff")).toMap());
    asset_report.insert(QStringLiteral("warnings"), image_validation.value(QStringLiteral("warnings")).toStringList());
    asset_reports.append(asset_report);
    asset_ids.append(asset_id);
    original_recipes.insert(asset_id, original_recipe_json);
    next_recipes.insert(asset_id, next_recipe_json);
  }
  if (!errors.isEmpty()) {
    setRunning(false);
    setStatus(cancellation_reason.isEmpty() ? QStringLiteral("Failed") : QStringLiteral("Canceled"));
    setErrorMessage(errors.join(QStringLiteral("; ")));
    auto report = result_map(false, errors);
    if (!cancellation_reason.isEmpty()) {
      report.insert(QStringLiteral("cancellationReason"), cancellation_reason);
    }
    report.insert(QStringLiteral("mode"), QStringLiteral("draft-apply"));
    report.insert(QStringLiteral("runId"), draft_run_id_);
    report.insert(
        QStringLiteral("scope"),
        report_scope);
    report.insert(QStringLiteral("assetReports"), asset_reports);
    report.insert(QStringLiteral("batchClusters"), batch_clusters);
    report.insert(QStringLiteral("clusterPlanning"), batch_plan_metadata.value(QStringLiteral("clusterPlanning")));
    report.insert(QStringLiteral("representativeAssetIds"), batch_plan_metadata.value(QStringLiteral("representativeAssetIds")));
    report.insert(QStringLiteral("clusterCount"), batch_clusters.size());
    report.insert(QStringLiteral("batchConcurrencyLimit"), kAgentBatchConcurrencyLimit);
    report.insert(
        QStringLiteral("batchSummary"),
        batch_summary(
            report.value(QStringLiteral("scope")).toString(),
            asset_ids,
            asset_reports,
            false,
            false));
    if (!cancellation_reason.isEmpty()) {
      insert_cancel_rollback_metadata(
          &report,
          draft_run_id_,
          report_scope,
          asset_ids,
          rollback_on_cancel_,
          0,
          0);
    }
    report.insert(QStringLiteral("styleId"), selected_style_id_);
    const auto model = model_metadata_by_id(available_models_, selected_model_id_);
    report.insert(QStringLiteral("modelId"), selected_model_id_);
    report.insert(QStringLiteral("modelBackend"), model.value(QStringLiteral("backend")));
    report.insert(QStringLiteral("recipeBaseMode"), base_mode);
    report.insert(QStringLiteral("manualEditPolicy"), manual_edit_policy);
    finalize_timing(&report, started_at_utc, run_timer.elapsed(), tool_calls);
    log_agent_tool_calls(draft_run_id_, tool_calls);
    write_agent_run_manifest(&report, style, model, selected_model_id_);
    if (cancellation_requested_) {
      cancellation_requested_ = false;
      emit cancellationChanged();
    }
    if (!cancellation_reason.isEmpty()) {
      rollback_on_cancel_ = false;
    }
    setLastRunReport(report);
    return report;
  }

  QStringList applied_ids;
  for (const auto& asset_id : asset_ids) {
    if (stop_requested()) {
      break;
    }
    const auto updated =
        catalog_model_->applyRecipeJsonToAssetIds({asset_id}, next_recipes.value(asset_id));
    if (updated != 1) {
      errors.append(QStringLiteral("Unable to apply draft recipe to selected photo: %1").arg(asset_id));
      break;
    }
    applied_ids.append(asset_id);
  }
  if (!errors.isEmpty()) {
    auto restored_count = 0;
    for (const auto& applied_id : applied_ids) {
      const auto restore_recipe =
          draft_original_recipes_.value(applied_id, original_recipes.value(applied_id));
      restored_count += catalog_model_->applyRecipeJsonToAssetIds({applied_id}, restore_recipe);
    }
    setRunning(false);
    setStatus(cancellation_reason.isEmpty() ? QStringLiteral("Failed") : QStringLiteral("Canceled"));
    setErrorMessage(errors.join(QStringLiteral("; ")));
    auto report = result_map(false, errors);
    if (!cancellation_reason.isEmpty()) {
      report.insert(QStringLiteral("cancellationReason"), cancellation_reason);
    }
    report.insert(QStringLiteral("mode"), QStringLiteral("draft-apply"));
    report.insert(QStringLiteral("runId"), draft_run_id_);
    report.insert(
        QStringLiteral("scope"),
        report_scope);
    report.insert(QStringLiteral("assetReports"), asset_reports);
    report.insert(QStringLiteral("batchClusters"), batch_clusters);
    report.insert(QStringLiteral("clusterPlanning"), batch_plan_metadata.value(QStringLiteral("clusterPlanning")));
    report.insert(QStringLiteral("representativeAssetIds"), batch_plan_metadata.value(QStringLiteral("representativeAssetIds")));
    report.insert(QStringLiteral("clusterCount"), batch_clusters.size());
    report.insert(QStringLiteral("batchConcurrencyLimit"), kAgentBatchConcurrencyLimit);
    report.insert(
        QStringLiteral("batchSummary"),
        batch_summary(
            report.value(QStringLiteral("scope")).toString(),
            asset_ids,
            asset_reports,
            false,
            false));
    if (!cancellation_reason.isEmpty()) {
      insert_cancel_rollback_metadata(
          &report,
          draft_run_id_,
          report_scope,
          applied_ids,
          rollback_on_cancel_,
          static_cast<int>(applied_ids.size()),
          restored_count);
    } else {
      report.insert(QStringLiteral("restoredCount"), restored_count);
    }
    report.insert(QStringLiteral("styleId"), selected_style_id_);
    const auto model = model_metadata_by_id(available_models_, selected_model_id_);
    report.insert(QStringLiteral("modelId"), selected_model_id_);
    report.insert(QStringLiteral("modelBackend"), model.value(QStringLiteral("backend")));
    report.insert(QStringLiteral("recipeBaseMode"), base_mode);
    report.insert(QStringLiteral("manualEditPolicy"), manual_edit_policy);
    append_tool_call(
        &tool_calls,
        QStringLiteral("apply_recipe_patch_to_selected"),
        QStringLiteral("failed"),
        QVariantMap{
            {QStringLiteral("assetCount"), asset_ids.size()},
            {QStringLiteral("appliedCount"), applied_ids.size()},
            {QStringLiteral("restoredCount"), restored_count},
            {QStringLiteral("recipeBaseMode"), base_mode},
        });
    finalize_timing(&report, started_at_utc, run_timer.elapsed(), tool_calls);
    log_agent_tool_calls(draft_run_id_, tool_calls);
    write_agent_run_manifest(&report, style, model, selected_model_id_);
    if (cancellation_requested_) {
      cancellation_requested_ = false;
      emit cancellationChanged();
    }
    if (!cancellation_reason.isEmpty()) {
      rollback_on_cancel_ = false;
    }
    setLastRunReport(report);
    return report;
  }

  for (auto iterator = original_recipes.cbegin(); iterator != original_recipes.cend(); ++iterator) {
    if (!draft_original_recipes_.contains(iterator.key())) {
      draft_original_recipes_.insert(iterator.key(), iterator.value());
    }
  }

  for (auto index = 0; index < asset_reports.size(); ++index) {
    auto asset_report = asset_reports.at(index).toMap();
    asset_report.insert(QStringLiteral("status"), QStringLiteral("drafted"));
    asset_reports[index] = asset_report;
  }
  batch_clusters = clusters_with_outlier_reports(batch_clusters, asset_reports);
  auto draft_preview_count = 0;
  auto settled_preview_count = 0;
  for (const auto& asset_report_value : asset_reports) {
    const auto asset_report = asset_report_value.toMap();
    draft_preview_count += asset_report.value(QStringLiteral("draftPreviewCount")).toInt();
    if (asset_report.value(QStringLiteral("settledPreviewReady")).toBool()) {
      ++settled_preview_count;
    }
  }

  QVariantMap report = result_map(true, {}, report_warnings);
  report.insert(QStringLiteral("warnings"), report_warnings);
  report.insert(QStringLiteral("runId"), draft_run_id_);
  report.insert(QStringLiteral("assetIds"), asset_ids);
  report.insert(QStringLiteral("scope"), report_scope);
  report.insert(QStringLiteral("draftedCount"), asset_ids.size());
  report.insert(QStringLiteral("styleId"), selected_style_id_);
  const auto model = model_metadata_by_id(available_models_, selected_model_id_);
  report.insert(QStringLiteral("modelId"), selected_model_id_);
  report.insert(QStringLiteral("modelBackend"), model.value(QStringLiteral("backend")));
  report.insert(QStringLiteral("mode"), QStringLiteral("draft-apply"));
  report.insert(QStringLiteral("recipeBaseMode"), base_mode);
  report.insert(QStringLiteral("manualEditPolicy"), manual_edit_policy);
  report.insert(QStringLiteral("editGoals"), agent_edit_goals(style, user_instructions_, first_final_patch));
  report.insert(QStringLiteral("riskAssessment"), first_risk_assessment);
  report.insert(QStringLiteral("requestedRecipePatch"), patch);
  report.insert(QStringLiteral("clusterPlanning"), batch_plan_metadata.value(QStringLiteral("clusterPlanning")));
  report.insert(QStringLiteral("batchClusters"), batch_clusters);
  report.insert(QStringLiteral("clusterCount"), batch_clusters.size());
  report.insert(QStringLiteral("representativeAssetIds"), batch_plan_metadata.value(QStringLiteral("representativeAssetIds")));
  report.insert(QStringLiteral("recipePatch"), first_final_patch.isEmpty() ? patch : first_final_patch);
  report.insert(QStringLiteral("recipeStorageMode"), QStringLiteral("recipe-diff-first"));
  report.insert(QStringLiteral("validationIterationLimit"), kMaxValidationRevisionIterations);
  report.insert(
      QStringLiteral("previewProofPolicy"),
      QVariantMap{
          {QStringLiteral("draftPreviewsDuringPlanning"), true},
          {QStringLiteral("draftPreviewPolicy"), QStringLiteral("render-each-validation-iteration")},
          {QStringLiteral("draftPreviewCount"), draft_preview_count},
          {QStringLiteral("settledPreviewsForFinalProof"), true},
          {QStringLiteral("settledPreviewPolicy"), QStringLiteral("final-validated-recipe-proof")},
          {QStringLiteral("settledPreviewCount"), settled_preview_count},
      });
  report.insert(QStringLiteral("assetReports"), asset_reports);
  report.insert(QStringLiteral("batchConcurrencyLimit"), kAgentBatchConcurrencyLimit);
  report.insert(
      QStringLiteral("batchSummary"),
      batch_summary(
          report.value(QStringLiteral("scope")).toString(),
          asset_ids,
          asset_reports,
          true,
          true));
  report.insert(
      QStringLiteral("batchTransaction"),
      batch_transaction(
          draft_run_id_,
          report.value(QStringLiteral("scope")).toString(),
          asset_ids,
          QStringLiteral("drafted"),
          true));
  const auto first_asset_report = asset_reports.isEmpty() ? QVariantMap{} : asset_reports.first().toMap();
  if (first_asset_report.contains(QStringLiteral("maskLocalProof"))) {
    report.insert(QStringLiteral("maskLocalProof"), first_asset_report.value(QStringLiteral("maskLocalProof")));
  }
  const auto first_validation = first_asset_report.value(QStringLiteral("validationReport")).toMap();
  report.insert(QStringLiteral("validationReport"), first_validation);
  const auto artifacts = first_validation.value(QStringLiteral("artifacts")).toMap();
  report.insert(QStringLiteral("artifacts"), artifacts);
  report.insert(
      QStringLiteral("artifactDirectory"),
      artifacts.value(QStringLiteral("artifactDirectory")));
  report.insert(QStringLiteral("beforeProofPath"), artifacts.value(QStringLiteral("beforeProofPath")));
  report.insert(QStringLiteral("afterProofPath"), artifacts.value(QStringLiteral("afterProofPath")));
  report.insert(QStringLiteral("recipeDiffPath"), artifacts.value(QStringLiteral("recipeDiffPath")));
  report.insert(
      QStringLiteral("validationReportPath"),
      artifacts.value(QStringLiteral("validationReportPath")));
  report.insert(QStringLiteral("recipeDiff"), first_validation.value(QStringLiteral("recipeDiff")));
  report.insert(QStringLiteral("draftActive"), true);
  mark_agent_draft_review_required(&report);
  append_tool_call(
      &tool_calls,
      QStringLiteral("apply_recipe_patch_to_selected"),
      QStringLiteral("ok"),
      QVariantMap{
          {QStringLiteral("assetCount"), asset_ids.size()},
          {QStringLiteral("draftedCount"), asset_ids.size()},
          {QStringLiteral("recipeBaseMode"), base_mode},
      });
  finalize_timing(&report, started_at_utc, run_timer.elapsed(), tool_calls);
  log_agent_tool_calls(draft_run_id_, tool_calls);
  write_agent_run_manifest(&report, style, model, selected_model_id_);
  observability::logInfo(
      photoEditorAppLog(),
      QStringLiteral("agent.edit.draft_applied"),
      {{QStringLiteral("run_id"), draft_run_id_},
       {QStringLiteral("asset_id"), asset_ids.value(0)},
       {QStringLiteral("asset_count"), asset_ids.size()},
       {QStringLiteral("style_id"), selected_style_id_}});
  observability::logInfo(
      photoEditorAppLog(),
      QStringLiteral("agent.edit.proof_artifacts_written"),
      {{QStringLiteral("run_id"), draft_run_id_},
       {QStringLiteral("asset_id"), asset_ids.value(0)},
       {QStringLiteral("asset_count"), asset_ids.size()},
       {QStringLiteral("has_before_after"), !report.value(QStringLiteral("afterProofPath")).toString().isEmpty()},
       {QStringLiteral("has_recipe_diff"), !report.value(QStringLiteral("recipeDiff")).toMap().isEmpty()}});
  setStatus(asset_ids.size() > 1 ? QStringLiteral("Batch draft ready") : QStringLiteral("Draft ready"));
  rollback_on_cancel_ = false;
  setRunning(false);
  setLastRunReport(report);
  emit draftChanged();
  return report;
}

QVariantMap AgentEditController::cancelRun(const QString& reason) {
  if (!running_) {
    return result_map(false, {QStringLiteral("No active agent run to cancel.")});
  }
  cancellation_reason_ =
      reason.trimmed().isEmpty() ? QStringLiteral("user_cancelled") : reason.trimmed();
  rollback_on_cancel_ = true;
  if (!cancellation_requested_) {
    cancellation_requested_ = true;
    emit cancellationChanged();
  }
  setStatus(QStringLiteral("Canceling"));
  QVariantMap report = result_map(true);
  report.insert(QStringLiteral("mode"), QStringLiteral("cancel"));
  report.insert(QStringLiteral("runId"), draft_run_id_);
  report.insert(QStringLiteral("cancellationReason"), cancellation_reason_);
  report.insert(QStringLiteral("rollbackOnCancel"), rollback_on_cancel_);
  setLastRunReport(report);
  return report;
}

QVariantMap AgentEditController::rollbackDraft() {
  QStringList errors;
  if (catalog_model_ == nullptr) {
    errors.append(QStringLiteral("Catalog model is not connected."));
  }
  auto asset_ids = draft_original_recipes_.keys();
  std::sort(asset_ids.begin(), asset_ids.end());
  auto restored_count = 0;
  if (errors.isEmpty()) {
    for (auto iterator = draft_original_recipes_.cbegin(); iterator != draft_original_recipes_.cend(); ++iterator) {
      restored_count += catalog_model_->applyRecipeJsonToAssetIds({iterator.key()}, iterator.value());
    }
  }
  if (!errors.isEmpty()) {
    const auto report = result_map(false, errors);
    setLastRunReport(report);
    return report;
  }
  QVariantMap report = result_map(true);
  report.insert(QStringLiteral("mode"), QStringLiteral("rollback"));
  report.insert(QStringLiteral("runId"), draft_run_id_);
  report.insert(QStringLiteral("assetIds"), asset_ids);
  report.insert(
      QStringLiteral("scope"),
      asset_ids.size() > 1 ? QStringLiteral("selected-assets") : QStringLiteral("current-photo"));
  report.insert(QStringLiteral("restoredCount"), restored_count);
  report.insert(QStringLiteral("draftActive"), false);
  report.insert(
      QStringLiteral("batchTransaction"),
      batch_transaction(
          draft_run_id_,
          report.value(QStringLiteral("scope")).toString(),
          asset_ids,
          QStringLiteral("rolled_back"),
          false));
  observability::logInfo(
      photoEditorAppLog(),
      QStringLiteral("agent.edit.rollback"),
      {{QStringLiteral("run_id"), draft_run_id_},
       {QStringLiteral("restored_count"), restored_count}});
  draft_original_recipes_.clear();
  draft_run_id_.clear();
  setStatus(QStringLiteral("Rolled back"));
  setLastRunReport(report);
  emit draftChanged();
  return report;
}

QVariantMap AgentEditController::commitDraft(bool confirmed) {
  if (draft_original_recipes_.isEmpty()) {
    const QStringList errors{QStringLiteral("No active agent draft to commit.")};
    auto report = result_map(false, errors);
    report.insert(QStringLiteral("mode"), QStringLiteral("commit"));
    report.insert(QStringLiteral("committedCount"), 0);
    report.insert(QStringLiteral("confirmed"), confirmed);
    setStatus(QStringLiteral("Failed"));
    setErrorMessage(errors.join(QStringLiteral("; ")));
    setLastRunReport(report);
    return report;
  }
  auto asset_ids = draft_original_recipes_.keys();
  std::sort(asset_ids.begin(), asset_ids.end());
  if (!confirmed) {
    const QStringList errors{QStringLiteral("Agent draft commit requires confirmed: true.")};
    auto report = result_map(false, errors);
    report.insert(QStringLiteral("mode"), QStringLiteral("commit"));
    report.insert(QStringLiteral("runId"), draft_run_id_);
    report.insert(QStringLiteral("assetIds"), asset_ids);
    report.insert(
        QStringLiteral("scope"),
        asset_ids.size() > 1 ? QStringLiteral("selected-assets") : QStringLiteral("current-photo"));
    report.insert(QStringLiteral("committedCount"), 0);
    report.insert(QStringLiteral("confirmed"), false);
    report.insert(QStringLiteral("confirmationRequired"), true);
    report.insert(QStringLiteral("draftActive"), true);
    QVariantList asset_reports;
    for (const auto& asset_id : asset_ids) {
      asset_reports.append(QVariantMap{
          {QStringLiteral("assetId"), asset_id},
          {QStringLiteral("status"), QStringLiteral("drafted")},
      });
    }
    report.insert(QStringLiteral("assetReports"), asset_reports);
    report.insert(QStringLiteral("batchConcurrencyLimit"), kAgentBatchConcurrencyLimit);
    report.insert(
        QStringLiteral("batchSummary"),
        batch_summary(
            report.value(QStringLiteral("scope")).toString(),
            asset_ids,
            asset_reports,
            true,
            true));
    report.insert(
        QStringLiteral("batchTransaction"),
        batch_transaction(
            draft_run_id_,
            report.value(QStringLiteral("scope")).toString(),
            asset_ids,
            QStringLiteral("awaiting_commit_confirmation"),
            true));
    setStatus(QStringLiteral("Waiting for commit approval"));
    setErrorMessage(errors.join(QStringLiteral("; ")));
    setLastRunReport(report);
    return report;
  }

  QVariantList asset_reports;
  for (const auto& asset_id : asset_ids) {
    asset_reports.append(QVariantMap{
        {QStringLiteral("assetId"), asset_id},
        {QStringLiteral("status"), QStringLiteral("committed")},
    });
  }
  QVariantMap report = result_map(true);
  report.insert(QStringLiteral("mode"), QStringLiteral("commit"));
  report.insert(QStringLiteral("runId"), draft_run_id_);
  report.insert(QStringLiteral("assetIds"), asset_ids);
  report.insert(
      QStringLiteral("scope"),
      asset_ids.size() > 1 ? QStringLiteral("selected-assets") : QStringLiteral("current-photo"));
  report.insert(QStringLiteral("committedCount"), asset_ids.size());
  report.insert(QStringLiteral("confirmed"), true);
  report.insert(QStringLiteral("draftActive"), false);
  report.insert(QStringLiteral("assetReports"), asset_reports);
  report.insert(QStringLiteral("batchConcurrencyLimit"), kAgentBatchConcurrencyLimit);
  report.insert(
      QStringLiteral("batchSummary"),
      batch_summary(
          report.value(QStringLiteral("scope")).toString(),
          asset_ids,
          asset_reports,
          false,
          false));
  report.insert(
      QStringLiteral("batchTransaction"),
      batch_transaction(
          draft_run_id_,
          report.value(QStringLiteral("scope")).toString(),
          asset_ids,
          QStringLiteral("committed"),
          false));
  observability::logInfo(
      photoEditorAppLog(),
      QStringLiteral("agent.edit.commit"),
      {{QStringLiteral("run_id"), draft_run_id_},
       {QStringLiteral("asset_count"), asset_ids.size()},
       {QStringLiteral("committed_count"), asset_ids.size()}});
  draft_original_recipes_.clear();
  draft_run_id_.clear();
  setStatus(QStringLiteral("Committed"));
  setLastRunReport(report);
  emit draftChanged();
  return report;
}

void AgentEditController::setStatus(const QString& status) {
  if (status_ == status) {
    return;
  }
  status_ = status;
  emit statusChanged();
}

void AgentEditController::setErrorMessage(const QString& error_message) {
  if (error_message_ == error_message) {
    return;
  }
  error_message_ = error_message;
  emit errorMessageChanged();
}

void AgentEditController::setRunning(bool running) {
  if (running_ == running) {
    return;
  }
  running_ = running;
  emit runningChanged();
}

void AgentEditController::setLastRunReport(const QVariantMap& report) {
  last_run_report_ = report;
  const auto history_entry = run_history_entry(report);
  if (!history_entry.isEmpty()) {
    run_history_.prepend(history_entry);
    while (run_history_.size() > 20) {
      run_history_.removeLast();
    }
    emit runHistoryChanged();
  }
  emit lastRunReportChanged();
}

QVariantMap AgentEditController::selectedStyle() const {
  for (const auto& value : available_styles_) {
    const auto style = value.toMap();
    if (style.value(QStringLiteral("id")).toString() == selected_style_id_) {
      return style;
    }
  }
  return {};
}

QString AgentEditController::styleDirectory() const {
  return QDir(AppBootstrap::appDataLocation()).filePath(QStringLiteral("agent-styles"));
}

}  // namespace photo_editor::desktop

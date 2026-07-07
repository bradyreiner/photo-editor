#include "agent_edit_controller.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <future>
#include <thread>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

#include "agent_edit_recipe_patch.h"
#include "agent_image_validation.h"
#include "agent_local_model.h"
#include "agent_memory.h"
#include "agent_mcp_client.h"
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
constexpr int kMaxModelToolCallsPerTurn = 3;
constexpr int kMaxModelRepairTurnsPerAsset = 2;
constexpr double kMinEditLoopScoreImprovement = 0.03;

using ProofCancellationCallback = std::function<bool()>;
using AgentCancellationCallback = std::function<bool()>;

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

QString stable_variant_hash(const QVariant& value) {
  const auto bytes = QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact);
  return QString::fromLatin1(
      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

double edit_loop_quality_score(const QVariantMap& validation_report) {
  auto quality_score = validation_report.value(QStringLiteral("qualityScore"), 0.0).toDouble();
  const auto style_match = validation_report.value(QStringLiteral("styleMatch")).toMap();
  if (!style_match.isEmpty() && style_match.value(QStringLiteral("available")).toBool()) {
    const auto style_score = style_match.value(QStringLiteral("score"), 1.0).toDouble();
    quality_score = std::min(quality_score, (quality_score * 0.72) + (style_score * 0.28));
  }
  if (!validation_report.value(QStringLiteral("ok")).toBool()) {
    return 0.0;
  }
  return std::clamp(quality_score, 0.0, 1.0);
}

QVariantMap edit_loop_budget_report() {
  return QVariantMap{
      {QStringLiteral("maxAttempts"), kMaxValidationRevisionIterations + 1},
      {QStringLiteral("maxValidationRevisionIterations"), kMaxValidationRevisionIterations},
      {QStringLiteral("maxModelRepairTurns"), kMaxModelRepairTurnsPerAsset},
      {QStringLiteral("minScoreImprovement"), kMinEditLoopScoreImprovement},
      {QStringLiteral("hardLimit"), true},
  };
}

QStringList ensure_agent_memory_vault_sections(const QString& root_path) {
  const QStringList sections{
      QStringLiteral("styles"),
      QStringLiteral("projects"),
      QStringLiteral("feedback"),
      QStringLiteral("runs"),
      QStringLiteral("tools"),
      QStringLiteral("issues"),
  };
  QStringList errors;
  for (const auto& section : sections) {
    const auto section_path = QDir(root_path).filePath(section);
    if (!QDir().mkpath(section_path)) {
      errors.append(QStringLiteral("Unable to create memory vault section: %1").arg(section_path));
    }
  }
  return errors;
}

QUrl obsidian_open_url_for_path(const QString& path) {
  QUrl url;
  url.setScheme(QStringLiteral("obsidian"));
  url.setHost(QStringLiteral("open"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("path"), path);
  url.setQuery(query);
  return url;
}

QVariantMap memory_snapshot_signatures(const QVariantMap& snapshot) {
  QVariantMap signatures;
  for (const auto& value : snapshot.value(QStringLiteral("files")).toList()) {
    const auto file = value.toMap();
    const auto relative_path = file.value(QStringLiteral("relativePath")).toString();
    if (!relative_path.trimmed().isEmpty()) {
      signatures.insert(relative_path, file.value(QStringLiteral("contentHash")));
    }
  }
  return signatures;
}

QVariantList memory_snapshot_changes(
    const QVariantMap& previous_snapshot,
    const QVariantMap& current_snapshot,
    int maximum_count = 16) {
  const auto previous = memory_snapshot_signatures(previous_snapshot);
  const auto current = memory_snapshot_signatures(current_snapshot);
  QSet<QString> key_set;
  for (auto iterator = previous.cbegin(); iterator != previous.cend(); ++iterator) {
    key_set.insert(iterator.key());
  }
  for (auto iterator = current.cbegin(); iterator != current.cend(); ++iterator) {
    key_set.insert(iterator.key());
  }
  QStringList keys;
  keys.reserve(key_set.size());
  for (const auto& key : key_set) {
    keys.append(key);
  }
  keys.sort();

  QVariantList changes;
  for (const auto& key : keys) {
    const auto previous_hash = previous.value(key).toString();
    const auto current_hash = current.value(key).toString();
    if (previous_hash == current_hash) {
      continue;
    }
    const auto change_type = previous_hash.isEmpty()
                                 ? QStringLiteral("added")
                                 : current_hash.isEmpty()
                                       ? QStringLiteral("deleted")
                                       : QStringLiteral("modified");
    changes.append(QVariantMap{
        {QStringLiteral("relativePath"), key},
        {QStringLiteral("changeType"), change_type},
    });
    if (changes.size() >= maximum_count) {
      break;
    }
  }
  return changes;
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

void insert_optional_storage_map(
    QVariantMap* document,
    const QVariantMap& style,
    const QString& storage_key,
    const QString& runtime_key) {
  if (document == nullptr) {
    return;
  }
  const auto value = style.value(storage_key, style.value(runtime_key));
  if (value.isValid() && !value.toMap().isEmpty()) {
    document->insert(storage_key, value);
  }
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
  insert_optional_storage_map(
      &document,
      style,
      QStringLiteral("learned_profile"),
      QStringLiteral("learnedProfile"));
  insert_optional_storage_map(
      &document,
      style,
      QStringLiteral("tool_knowledge"),
      QStringLiteral("toolKnowledge"));
  insert_optional_storage_map(
      &document,
      style,
      QStringLiteral("quality_model"),
      QStringLiteral("qualityModel"));
  insert_optional_storage_map(
      &document,
      style,
      QStringLiteral("project_workflow"),
      QStringLiteral("projectWorkflow"));
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

void add_stat_sample(QVariantMap* sums, const QString& key, double value) {
  if (sums == nullptr) {
    return;
  }
  sums->insert(key, sums->value(key).toDouble() + value);
  const auto count_key = QStringLiteral("%1Count").arg(key);
  sums->insert(count_key, sums->value(count_key).toInt() + 1);
}

QVariantMap averaged_recipe_tendencies(const QVariantMap& sums) {
  QVariantMap tendencies;
  const QStringList keys{
      QStringLiteral("exposure"),
      QStringLiteral("contrast"),
      QStringLiteral("highlights"),
      QStringLiteral("shadows"),
      QStringLiteral("temperature"),
      QStringLiteral("tint"),
      QStringLiteral("vibrance"),
      QStringLiteral("saturation"),
      QStringLiteral("clarity"),
      QStringLiteral("dehaze"),
      QStringLiteral("cropZoom"),
  };
  for (const auto& key : keys) {
    const auto count = sums.value(QStringLiteral("%1Count").arg(key)).toInt();
    if (count > 0) {
      tendencies.insert(QStringLiteral("average%1%2")
                            .arg(key.left(1).toUpper(), key.mid(1)),
                        sums.value(key).toDouble() / static_cast<double>(count));
    }
  }
  tendencies.insert(QStringLiteral("maskUsageCount"), sums.value(QStringLiteral("maskUsageCount")).toInt());
  tendencies.insert(QStringLiteral("linearMaskUsageCount"), sums.value(QStringLiteral("linearMaskUsageCount")).toInt());
  tendencies.insert(QStringLiteral("radialMaskUsageCount"), sums.value(QStringLiteral("radialMaskUsageCount")).toInt());
  tendencies.insert(QStringLiteral("layerUsageCount"), sums.value(QStringLiteral("layerUsageCount")).toInt());
  return tendencies;
}

double style_score_component(double actual, double target, double tolerance) {
  if (!std::isfinite(actual) || !std::isfinite(target) || tolerance <= 0.0) {
    return 1.0;
  }
  return std::clamp(1.0 - (std::abs(actual - target) / tolerance), 0.0, 1.0);
}

QVariantMap visual_style_fingerprint_from_validation(const QVariantMap& validation_report) {
  return QVariantMap{
      {QStringLiteral("averageLuma"), validation_report.value(QStringLiteral("averageLuma"))},
      {QStringLiteral("lumaStddev"), validation_report.value(QStringLiteral("lumaStddev"))},
      {QStringLiteral("lumaRange"), validation_report.value(QStringLiteral("lumaRange"))},
      {QStringLiteral("averageRed"), validation_report.value(QStringLiteral("averageRed"))},
      {QStringLiteral("averageGreen"), validation_report.value(QStringLiteral("averageGreen"))},
      {QStringLiteral("averageBlue"), validation_report.value(QStringLiteral("averageBlue"))},
      {QStringLiteral("channelImbalance"), validation_report.value(QStringLiteral("channelImbalance"))},
      {QStringLiteral("redBlueCast"), validation_report.value(QStringLiteral("redBlueCast"))},
      {QStringLiteral("greenCast"), validation_report.value(QStringLiteral("greenCast"))},
      {QStringLiteral("saturationExtremePercent"), validation_report.value(QStringLiteral("saturationExtremePercent"))},
      {QStringLiteral("skinToneCandidatePercent"), validation_report.value(QStringLiteral("skinToneCandidatePercent"))},
      {QStringLiteral("averageSkinHue"), validation_report.value(QStringLiteral("averageSkinHue"))},
      {QStringLiteral("averageSkinSaturation"), validation_report.value(QStringLiteral("averageSkinSaturation"))},
      {QStringLiteral("averageSkinLuma"), validation_report.value(QStringLiteral("averageSkinLuma"))},
  };
}

QVariantMap style_learning_candidate_summary(const QVariantList& assets) {
  auto usable_recipe_count = 0;
  auto edited_reference_count = 0;
  QStringList example_asset_ids;
  for (const auto& asset_value : assets) {
    const auto asset = asset_value.toMap();
    const auto asset_id = asset.value(QStringLiteral("assetId")).toString();
    if (!asset_id.isEmpty() && example_asset_ids.size() < 6) {
      example_asset_ids.append(asset_id);
    }
    const auto recipe_json = asset.value(QStringLiteral("recipeJson")).toString();
    if (validate_edit_recipe_json(recipe_json).valid) {
      ++usable_recipe_count;
    }
    const auto source_asset_id = asset.value(QStringLiteral("sourceAssetId")).toString().trimmed();
    const auto variant_name = asset.value(QStringLiteral("variantName")).toString().trimmed();
    if (asset.value(QStringLiteral("virtualCopy")).toBool() ||
        (!source_asset_id.isEmpty() && source_asset_id != asset_id) ||
        (!variant_name.isEmpty() && variant_name.compare(QStringLiteral("Original"), Qt::CaseInsensitive) != 0)) {
      ++edited_reference_count;
    }
  }
  const auto available = !assets.isEmpty() && usable_recipe_count > 0;
  QString reason;
  if (assets.isEmpty()) {
    reason = QStringLiteral("Select edited examples to learn a style.");
  } else if (usable_recipe_count <= 0) {
    reason = QStringLiteral("Selected photos do not have usable edit recipes yet.");
  } else if (edited_reference_count <= 0) {
    reason = QStringLiteral("Selected photos look usable, but edited references are stronger style examples.");
  } else {
    reason = QStringLiteral("Selected edited references can train a style.");
  }
  return QVariantMap{
      {QStringLiteral("available"), available},
      {QStringLiteral("selectedCount"), assets.size()},
      {QStringLiteral("usableRecipeCount"), usable_recipe_count},
      {QStringLiteral("editedReferenceCount"), edited_reference_count},
      {QStringLiteral("confidence"),
       usable_recipe_count >= 8 ? QStringLiteral("high")
                                : usable_recipe_count >= 3 ? QStringLiteral("medium")
                                                           : QStringLiteral("low")},
      {QStringLiteral("reason"), reason},
      {QStringLiteral("exampleAssetIds"), example_asset_ids},
      {QStringLiteral("recommendedAction"), available ? QStringLiteral("learn_style")
                                                       : QStringLiteral("choose_style")},
      {QStringLiteral("label"), available ? QStringLiteral("Learn This Style")
                                          : QStringLiteral("Choose Style")},
  };
}

QVariantMap style_taste_card_from_style(const QVariantMap& style) {
  const auto learned_profile = style.value(QStringLiteral("learnedProfile")).toMap().isEmpty()
                                   ? style.value(QStringLiteral("learned_profile")).toMap()
                                   : style.value(QStringLiteral("learnedProfile")).toMap();
  const auto reference_count = learned_profile.value(QStringLiteral("source_asset_count")).toInt();
  const auto usable_count = learned_profile.value(QStringLiteral("usable_recipe_count")).toInt();
  const auto fingerprint_count =
      learned_profile.value(QStringLiteral("visualFingerprintSummary")).toMap().isEmpty()
          ? learned_profile.value(QStringLiteral("visual_fingerprint_summary")).toMap()
                .value(QStringLiteral("referenceFingerprintCount")).toInt()
          : learned_profile.value(QStringLiteral("visualFingerprintSummary")).toMap()
                .value(QStringLiteral("referenceFingerprintCount")).toInt();
  QStringList example_asset_ids;
  for (const auto& reference_value : learned_profile.value(QStringLiteral("reference_examples")).toList()) {
    const auto reference = reference_value.toMap();
    const auto asset_id = reference.value(QStringLiteral("assetId")).toString();
    if (!asset_id.isEmpty() && example_asset_ids.size() < 6) {
      example_asset_ids.append(asset_id);
    }
  }
  const auto learned = !learned_profile.isEmpty();
  const auto confidence = !learned
                              ? QStringLiteral("built-in")
                              : usable_count >= 8 && fingerprint_count >= 4
                                    ? QStringLiteral("high")
                                    : usable_count >= 3 || fingerprint_count >= 2
                                          ? QStringLiteral("medium")
                                          : QStringLiteral("low");
  auto summary = learned_profile.value(QStringLiteral("styleSummary")).toString().trimmed();
  if (summary.isEmpty()) {
    summary = style.value(QStringLiteral("goal")).toString().trimmed();
  }
  return QVariantMap{
      {QStringLiteral("available"), !style.isEmpty()},
      {QStringLiteral("learned"), learned},
      {QStringLiteral("name"), style.value(QStringLiteral("name"))},
      {QStringLiteral("summary"), summary},
      {QStringLiteral("referenceCount"), reference_count},
      {QStringLiteral("usableReferenceCount"), usable_count},
      {QStringLiteral("confidence"), confidence},
      {QStringLiteral("exampleAssetIds"), example_asset_ids},
      {QStringLiteral("exampleCount"), example_asset_ids.size()},
      {QStringLiteral("beforeAfterPairCount"), learned_profile.value(QStringLiteral("before_after_pair_count")).toInt()},
  };
}

QVariantMap style_match_report_for_validation(
    const QVariantMap& validation_report,
    const QVariantMap& recipe_summary,
    const QVariantMap& style) {
  const auto learned_profile = style.value(QStringLiteral("learnedProfile")).toMap().isEmpty()
                                   ? style.value(QStringLiteral("learned_profile")).toMap()
                                   : style.value(QStringLiteral("learnedProfile")).toMap();
  const auto tendencies = learned_profile.value(QStringLiteral("editTendencies")).toMap();
  const auto fingerprint_summary =
      learned_profile.value(QStringLiteral("visualFingerprintSummary")).toMap().isEmpty()
          ? learned_profile.value(QStringLiteral("visual_fingerprint_summary")).toMap()
          : learned_profile.value(QStringLiteral("visualFingerprintSummary")).toMap();
  QVariantMap report{
      {QStringLiteral("available"), !learned_profile.isEmpty()},
      {QStringLiteral("method"), QStringLiteral("metadata_recipe_visual_fingerprint_v1")},
      {QStringLiteral("visualFingerprint"), visual_style_fingerprint_from_validation(validation_report)},
  };
  if (learned_profile.isEmpty()) {
    report.insert(QStringLiteral("score"), 1.0);
    report.insert(QStringLiteral("confidence"), QStringLiteral("none"));
    report.insert(QStringLiteral("reason"), QStringLiteral("No learned style profile is active."));
    return report;
  }

  QVariantList components;
  auto total = 0.0;
  auto count = 0;
  const auto add_component = [&](const QString& name, double actual, double target, double tolerance) {
    const auto score = style_score_component(actual, target, tolerance);
    total += score;
    ++count;
    components.append(QVariantMap{
        {QStringLiteral("name"), name},
        {QStringLiteral("actual"), actual},
        {QStringLiteral("target"), target},
        {QStringLiteral("tolerance"), tolerance},
        {QStringLiteral("score"), score},
    });
  };
  if (tendencies.contains(QStringLiteral("averageExposure"))) {
    add_component(
        QStringLiteral("recipe.exposure"),
        recipe_summary.value(QStringLiteral("exposure")).toDouble(),
        tendencies.value(QStringLiteral("averageExposure")).toDouble(),
        0.75);
  }
  if (tendencies.contains(QStringLiteral("averageTemperature"))) {
    add_component(
        QStringLiteral("recipe.temperature"),
        recipe_summary.value(QStringLiteral("temperature")).toDouble(),
        tendencies.value(QStringLiteral("averageTemperature")).toDouble(),
        900.0);
  }
  if (tendencies.contains(QStringLiteral("averageVibrance"))) {
    add_component(
        QStringLiteral("recipe.vibrance"),
        recipe_summary.value(QStringLiteral("vibrance")).toDouble(),
        tendencies.value(QStringLiteral("averageVibrance")).toDouble(),
        18.0);
  }
  if (tendencies.contains(QStringLiteral("averageSaturation"))) {
    add_component(
        QStringLiteral("recipe.saturation"),
        recipe_summary.value(QStringLiteral("saturation")).toDouble(),
        tendencies.value(QStringLiteral("averageSaturation")).toDouble(),
        18.0);
  }
  add_component(
      QStringLiteral("visual.lumaStddev"),
      validation_report.value(QStringLiteral("lumaStddev")).toDouble(),
      fingerprint_summary.value(QStringLiteral("averageReferenceLumaStddev"),
                                validation_report.value(QStringLiteral("lumaStddev"))).toDouble(),
      24.0);
  add_component(
      QStringLiteral("visual.channelImbalance"),
      validation_report.value(QStringLiteral("channelImbalance")).toDouble(),
      fingerprint_summary.value(QStringLiteral("averageReferenceChannelImbalance"),
                                validation_report.value(QStringLiteral("channelImbalance"))).toDouble(),
      0.18);

  const auto score = count <= 0 ? 1.0 : total / static_cast<double>(count);
  report.insert(QStringLiteral("score"), score);
  report.insert(QStringLiteral("scorePercent"), static_cast<int>(std::round(score * 100.0)));
  report.insert(QStringLiteral("confidence"), count >= 4 ? QStringLiteral("medium") : QStringLiteral("low"));
  report.insert(QStringLiteral("componentCount"), count);
  report.insert(QStringLiteral("components"), components);
  report.insert(QStringLiteral("referenceAssetCount"), learned_profile.value(QStringLiteral("source_asset_count")));
  report.insert(QStringLiteral("needsReview"), score < 0.62);
  if (score < 0.62) {
    report.insert(QStringLiteral("reason"), QStringLiteral("Rendered proof differs from the learned recipe/visual style fingerprint."));
  }
  return report;
}

QVariantList inferred_before_after_pairs(const QVariantList& assets) {
  QVariantMap assets_by_id;
  for (const auto& value : assets) {
    const auto asset = value.toMap();
    const auto asset_id = asset.value(QStringLiteral("assetId")).toString();
    if (!asset_id.isEmpty()) {
      assets_by_id.insert(asset_id, asset);
    }
  }
  QVariantList pairs;
  QSet<QString> seen;
  for (const auto& value : assets) {
    const auto asset = value.toMap();
    const auto asset_id = asset.value(QStringLiteral("assetId")).toString();
    auto source_id = asset.value(QStringLiteral("sourceAssetId")).toString();
    if (source_id.isEmpty() && asset.value(QStringLiteral("virtualCopy")).toBool()) {
      for (const auto& candidate_value : assets) {
        const auto candidate = candidate_value.toMap();
        const auto candidate_id = candidate.value(QStringLiteral("assetId")).toString();
        if (!candidate_id.isEmpty() && candidate_id != asset_id &&
            !candidate.value(QStringLiteral("virtualCopy")).toBool()) {
          source_id = candidate_id;
          break;
        }
      }
    }
    if (asset_id.isEmpty() || source_id.isEmpty() || source_id == asset_id ||
        !assets_by_id.contains(source_id)) {
      continue;
    }
    const auto pair_key = QStringLiteral("%1->%2").arg(source_id, asset_id);
    if (seen.contains(pair_key)) {
      continue;
    }
    seen.insert(pair_key);
    const auto before_asset = assets_by_id.value(source_id).toMap();
    pairs.append(QVariantMap{
        {QStringLiteral("beforeAssetId"), source_id},
        {QStringLiteral("afterAssetId"), asset_id},
        {QStringLiteral("relationship"), QStringLiteral("sourceAssetId")},
        {QStringLiteral("beforeFilePath"), before_asset.value(QStringLiteral("filePath"))},
        {QStringLiteral("afterFilePath"), asset.value(QStringLiteral("filePath"))},
        {QStringLiteral("beforeRecipeBytes"), before_asset.value(QStringLiteral("recipeJson")).toString().toUtf8().size()},
        {QStringLiteral("afterRecipeBytes"), asset.value(QStringLiteral("recipeJson")).toString().toUtf8().size()},
    });
  }
  if (pairs.isEmpty() && assets.size() == 2) {
    const auto before_asset = assets.at(0).toMap();
    const auto after_asset = assets.at(1).toMap();
    const auto before_id = before_asset.value(QStringLiteral("assetId")).toString();
    const auto after_id = after_asset.value(QStringLiteral("assetId")).toString();
    if (!before_id.isEmpty() && !after_id.isEmpty() && before_id != after_id) {
      pairs.append(QVariantMap{
          {QStringLiteral("beforeAssetId"), before_id},
          {QStringLiteral("afterAssetId"), after_id},
          {QStringLiteral("relationship"), QStringLiteral("selected_reference_pair")},
          {QStringLiteral("confidence"), QStringLiteral("low")},
          {QStringLiteral("beforeFilePath"), before_asset.value(QStringLiteral("filePath"))},
          {QStringLiteral("afterFilePath"), after_asset.value(QStringLiteral("filePath"))},
          {QStringLiteral("beforeRecipeBytes"), before_asset.value(QStringLiteral("recipeJson")).toString().toUtf8().size()},
          {QStringLiteral("afterRecipeBytes"), after_asset.value(QStringLiteral("recipeJson")).toString().toUtf8().size()},
      });
    }
  }
  return pairs;
}

QVariantMap default_agent_tool_knowledge() {
  return QVariantMap{
      {QStringLiteral("globalTone"),
       QStringLiteral("Use exposure, contrast, highlights, shadows, whites, and blacks for the baseline read of the photo. Protect highlight detail and avoid crushed blacks before adding style.")},
      {QStringLiteral("color"),
       QStringLiteral("Use temperature, tint, vibrance, and saturation for the global color direction. Keep skin and neutrals believable unless the style profile explicitly pushes color.")},
      {QStringLiteral("curves"),
       QStringLiteral("Use curves for contrast shape and tonal separation after global exposure is stable. Keep curve moves subtle for batch consistency.")},
      {QStringLiteral("hsl"),
       QStringLiteral("Use HSL/color mixer for targeted hue, saturation, and luminance changes. Treat oranges/reds as skin-sensitive and blues/greens as scene-sensitive.")},
      {QStringLiteral("colorGrading"),
       QStringLiteral("Use color grading for mood and palette harmony. Keep saturation within style constraints and avoid contaminating skin or product color.")},
      {QStringLiteral("masks"),
       QStringLiteral("Use masks when subject, sky, background, or local light needs a separate treatment. Prefer masks when a global adjustment would damage another region.")},
      {QStringLiteral("detail"),
       QStringLiteral("Use clarity, texture, sharpening, denoise, and grain to support the style without making skin brittle or edges artificial.")},
      {QStringLiteral("crop"),
       QStringLiteral("Use crop and straighten for composition, horizon, and subject emphasis. Keep batch crops conservative unless the user asks for a stronger composition pass.")},
      {QStringLiteral("retouch"),
       QStringLiteral("Use retouch only under explicit style/user permission. Treat inpaint, object removal, and destructive cleanup as review-required operations.")},
  };
}

QVariantMap default_agent_quality_model() {
  return QVariantMap{
      {QStringLiteral("priorities"),
       QStringList{
           QStringLiteral("Preserve subject readability and intentional composition."),
           QStringLiteral("Protect highlight and shadow detail unless the style deliberately clips."),
           QStringLiteral("Keep skin tones, neutrals, and product colors believable for the selected genre."),
           QStringLiteral("Create color harmony that matches the style profile rather than generic prettiness."),
           QStringLiteral("Maintain consistency across a selected project while correcting outliers individually."),
       }},
      {QStringLiteral("reviewSignals"),
       QStringList{
           QStringLiteral("clipping risk"),
           QStringLiteral("skin hue drift"),
           QStringLiteral("oversaturation"),
           QStringLiteral("mask or retouch uncertainty"),
           QStringLiteral("style-reference mismatch"),
       }},
  };
}

QVariantMap default_agent_project_workflow() {
  return QVariantMap{
      {QStringLiteral("planBeforeBulkEdit"), true},
      {QStringLiteral("clusterByLighting"), true},
      {QStringLiteral("editRepresentativeSamplesFirst"), true},
      {QStringLiteral("validateEveryImage"), true},
      {QStringLiteral("repairValidationWarnings"), true},
      {QStringLiteral("reviewRiskyImagesBeforeCommit"), true},
      {QStringLiteral("saveAuditManifest"), true},
  };
}

QVariantMap learned_style_profile_from_assets(
    const QVariantList& assets,
    const QString& notes,
    const QString& goal) {
  QVariantList references;
  QVariantMap sums;
  auto usable_recipe_count = 0;
  auto skipped_recipe_count = 0;
  for (const auto& asset_value : assets) {
    const auto asset = asset_value.toMap();
    const auto recipe_json = asset.value(QStringLiteral("recipeJson")).toString();
    const auto validation = validate_edit_recipe_json(recipe_json);
    QVariantMap reference{
        {QStringLiteral("assetId"), asset.value(QStringLiteral("assetId"))},
        {QStringLiteral("sourceAssetId"), asset.value(QStringLiteral("sourceAssetId"))},
        {QStringLiteral("virtualCopy"), asset.value(QStringLiteral("virtualCopy"))},
        {QStringLiteral("variantName"), asset.value(QStringLiteral("variantName"))},
        {QStringLiteral("filePath"), asset.value(QStringLiteral("filePath"))},
        {QStringLiteral("cameraLabel"), asset.value(QStringLiteral("cameraLabel"))},
        {QStringLiteral("lensModel"), asset.value(QStringLiteral("lensModel"))},
        {QStringLiteral("captureDate"), asset.value(QStringLiteral("captureDate"))},
        {QStringLiteral("projectSummary"), asset.value(QStringLiteral("projectSummary"))},
        {QStringLiteral("recipeJsonBytes"), recipe_json.toUtf8().size()},
    };
    if (validation.valid) {
      const auto state = parse_edit_recipe_json(recipe_json);
      const auto summary = recipe_analysis_summary(state, recipe_json);
      reference.insert(QStringLiteral("recipeSummary"), summary);
      QImage source_image;
      if (decode_source_image(
              asset.value(QStringLiteral("filePath")).toString(),
              &source_image,
              kAgentProofPreviewMaxEdge,
              state)) {
        const auto rendered = render_output_image(
            source_image,
            state,
            preview_color_space(),
            QImage::Format_ARGB32);
        if (!rendered.isNull()) {
          const auto visual_report = validate_agent_edit_image(rendered, state, QVariantMap{});
          const auto fingerprint = visual_style_fingerprint_from_validation(visual_report);
          reference.insert(QStringLiteral("visualFingerprint"), fingerprint);
          add_stat_sample(
              &sums,
              QStringLiteral("referenceLumaStddev"),
              visual_report.value(QStringLiteral("lumaStddev")).toDouble());
          add_stat_sample(
              &sums,
              QStringLiteral("referenceChannelImbalance"),
              visual_report.value(QStringLiteral("channelImbalance")).toDouble());
        }
      }
      add_stat_sample(&sums, QStringLiteral("exposure"), state.exposure);
      add_stat_sample(&sums, QStringLiteral("contrast"), state.contrast);
      add_stat_sample(&sums, QStringLiteral("highlights"), state.highlights);
      add_stat_sample(&sums, QStringLiteral("shadows"), state.shadows);
      add_stat_sample(&sums, QStringLiteral("temperature"), state.temperature);
      add_stat_sample(&sums, QStringLiteral("tint"), state.tint);
      add_stat_sample(&sums, QStringLiteral("vibrance"), state.vibrance);
      add_stat_sample(&sums, QStringLiteral("saturation"), state.saturation);
      add_stat_sample(&sums, QStringLiteral("clarity"), state.clarity);
      add_stat_sample(&sums, QStringLiteral("dehaze"), state.dehaze);
      add_stat_sample(&sums, QStringLiteral("cropZoom"), state.crop_zoom);
      if (!state.mask_stack.isEmpty()) {
        sums.insert(QStringLiteral("maskUsageCount"), sums.value(QStringLiteral("maskUsageCount")).toInt() + 1);
      }
      if (state.linear_mask_enabled) {
        sums.insert(QStringLiteral("linearMaskUsageCount"), sums.value(QStringLiteral("linearMaskUsageCount")).toInt() + 1);
      }
      if (state.radial_mask_enabled) {
        sums.insert(QStringLiteral("radialMaskUsageCount"), sums.value(QStringLiteral("radialMaskUsageCount")).toInt() + 1);
      }
      if (!state.layers.isEmpty()) {
        sums.insert(QStringLiteral("layerUsageCount"), sums.value(QStringLiteral("layerUsageCount")).toInt() + 1);
      }
      ++usable_recipe_count;
    } else {
      reference.insert(QStringLiteral("recipeWarnings"), validation.errors);
      ++skipped_recipe_count;
    }
    if (references.size() < 24) {
      references.append(reference);
    }
  }

  const auto tendencies = averaged_recipe_tendencies(sums);
  auto visual_fingerprint_summary = QVariantMap{
      {QStringLiteral("averageReferenceLumaStddev"),
       sums.value(QStringLiteral("referenceLumaStddevCount")).toInt() > 0
           ? sums.value(QStringLiteral("referenceLumaStddev")).toDouble() /
                 static_cast<double>(sums.value(QStringLiteral("referenceLumaStddevCount")).toInt())
           : 0.0},
      {QStringLiteral("averageReferenceChannelImbalance"),
       sums.value(QStringLiteral("referenceChannelImbalanceCount")).toInt() > 0
           ? sums.value(QStringLiteral("referenceChannelImbalance")).toDouble() /
                 static_cast<double>(sums.value(QStringLiteral("referenceChannelImbalanceCount")).toInt())
           : 0.0},
      {QStringLiteral("referenceFingerprintCount"),
       sums.value(QStringLiteral("referenceLumaStddevCount")).toInt()},
      {QStringLiteral("method"), QStringLiteral("rendered_reference_fingerprint_v1")},
  };
  const auto before_after_pairs = inferred_before_after_pairs(assets);
  return QVariantMap{
      {QStringLiteral("source"), QStringLiteral("selected_project_assets")},
      {QStringLiteral("createdAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
      {QStringLiteral("source_asset_count"), assets.size()},
      {QStringLiteral("usable_recipe_count"), usable_recipe_count},
      {QStringLiteral("skipped_recipe_count"), skipped_recipe_count},
      {QStringLiteral("userNotes"), notes.trimmed()},
      {QStringLiteral("styleSummary"),
       QStringLiteral("Learned from %1 selected edited photo(s). Match the user's demonstrated edit tendencies while still obeying the stated goal: %2")
           .arg(usable_recipe_count)
           .arg(goal.trimmed())},
      {QStringLiteral("editTendencies"), tendencies},
      {QStringLiteral("visualFingerprintSummary"), visual_fingerprint_summary},
      {QStringLiteral("before_after_pairs"), before_after_pairs},
      {QStringLiteral("before_after_pair_count"), before_after_pairs.size()},
      {QStringLiteral("reference_examples"), references},
      {QStringLiteral("consistencyGuidelines"),
       QStringList{
           QStringLiteral("Treat the reference examples as the user's edited taste profile."),
           QStringLiteral("Match the average tonal/color direction unless an individual image needs a correction."),
           QStringLiteral("Prefer consistent batch baseline edits, then correct outliers individually."),
           QStringLiteral("When the style profile conflicts with validation, preserve image quality and ask for review."),
       }},
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
    const QVariantMap& recipe_patch = {},
    const QVariantMap& scope_context = {}) {
  QVariantList risks;
  QStringList approval_types;
  QVariantList approval_requests;
  const auto append_approval = [&](const QString& type, const QString& label, const QString& reason) {
    if (!approval_types.contains(type)) {
      approval_types.append(type);
      approval_requests.append(QVariantMap{
          {QStringLiteral("type"), type},
          {QStringLiteral("label"), label},
          {QStringLiteral("reason"), reason},
          {QStringLiteral("required"), true},
      });
    }
  };
  const auto append_risk = [&](const QString& type,
                               const QString& severity,
                               const QString& detail,
                               const QString& approval_type = QString(),
                               const QString& approval_label = QString()) {
    QVariantMap risk{
        {QStringLiteral("type"), type},
        {QStringLiteral("severity"), severity},
        {QStringLiteral("detail"), detail},
    };
    if (!approval_type.trimmed().isEmpty()) {
      risk.insert(QStringLiteral("approvalRequired"), true);
      risk.insert(QStringLiteral("approvalType"), approval_type);
      append_approval(approval_type, approval_label.trimmed().isEmpty() ? type : approval_label, detail);
    } else {
      risk.insert(QStringLiteral("approvalRequired"), false);
    }
    risks.append(risk);
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
  const auto crop_patch = recipe_patch.value(QStringLiteral("crop")).toMap();
  const auto crop_zoom = crop_patch.value(QStringLiteral("cropZoom"), 1.0).toDouble();
  const auto crop_width = crop_patch.value(QStringLiteral("cropWidth"), 1.0).toDouble();
  const auto crop_height = crop_patch.value(QStringLiteral("cropHeight"), 1.0).toDouble();
  const auto has_crop_offset =
      std::abs(crop_patch.value(QStringLiteral("cropX")).toDouble()) > 0.05 ||
      std::abs(crop_patch.value(QStringLiteral("cropY")).toDouble()) > 0.05;
  if (!crop_patch.isEmpty() &&
      (crop_zoom >= 1.15 || crop_width <= 0.90 || crop_height <= 0.90 || has_crop_offset)) {
    append_risk(
        QStringLiteral("largeCrop"),
        QStringLiteral("approval"),
        QStringLiteral("Planned patch makes a large crop or composition change."),
        QStringLiteral("large_crop"),
        QStringLiteral("Large crop"));
  }
  if (recipe_patch.contains(QStringLiteral("retouch"))) {
    append_risk(
        QStringLiteral("retouch"),
        QStringLiteral("approval"),
        QStringLiteral("Planned patch uses retouching or object cleanup."),
        QStringLiteral("retouch"),
        QStringLiteral("Retouch"));
  }
  if (recipe_patch.contains(QStringLiteral("masks")) ||
      recipe_patch.contains(QStringLiteral("retouch")) ||
      recipe_patch.contains(QStringLiteral("layers"))) {
    append_risk(
        QStringLiteral("localizedOrDestructiveTools"),
        QStringLiteral("review"),
        QStringLiteral("Planned patch touches masks, retouch, layers, or LUT stacks."));
  }
  const auto outside_project_asset_ids =
      scope_context.value(QStringLiteral("outsideActiveProjectAssetIds")).toStringList();
  const auto scope_escapes_active_project =
      scope_context.value(QStringLiteral("scopeEscapesActiveProject")).toBool();
  if (!outside_project_asset_ids.isEmpty() || scope_escapes_active_project) {
    const auto active_project_name =
        scope_context.value(QStringLiteral("activeProjectName")).toString();
    const auto detail =
        outside_project_asset_ids.isEmpty()
            ? QStringLiteral("Agent operation uses %1 scope while active project%2 is open.")
                  .arg(scope_context.value(QStringLiteral("requestedScope")).toString())
                  .arg(active_project_name.isEmpty()
                           ? QString()
                           : QStringLiteral(" %1").arg(active_project_name))
            : QStringLiteral("Agent operation includes %1 photo%2 outside the active project%3.")
                  .arg(static_cast<int>(outside_project_asset_ids.size()))
                  .arg(outside_project_asset_ids.size() == 1 ? QString() : QStringLiteral("s"))
                  .arg(active_project_name.isEmpty()
                           ? QString()
                           : QStringLiteral(": %1").arg(active_project_name));
    append_risk(
        QStringLiteral("outsideActiveProjectScope"),
        QStringLiteral("approval"),
        detail,
        QStringLiteral("outside_project_scope"),
        QStringLiteral("Outside project scope"));
    auto risk = risks.last().toMap();
    risk.insert(QStringLiteral("activeProjectName"), active_project_name);
    risk.insert(QStringLiteral("outsideAssetIds"), outside_project_asset_ids);
    risk.insert(QStringLiteral("scopeEscapesActiveProject"), scope_escapes_active_project);
    risks[static_cast<int>(risks.size()) - 1] = risk;
  }

  return QVariantMap{
      {QStringLiteral("riskCount"), risks.size()},
      {QStringLiteral("risks"), risks},
      {QStringLiteral("requiresReview"), !risks.isEmpty()},
      {QStringLiteral("approvalRequired"), !approval_types.isEmpty()},
      {QStringLiteral("approvalTypes"), approval_types},
      {QStringLiteral("approvalRequests"), approval_requests},
      {QStringLiteral("scopeContext"), scope_context},
  };
}

bool confidence_value_is_low(const QVariant& value) {
  if (!value.isValid() || value.isNull()) {
    return false;
  }
  bool ok = false;
  auto numeric_confidence = value.toDouble(&ok);
  if (ok) {
    if (numeric_confidence > 1.0 && numeric_confidence <= 100.0) {
      numeric_confidence /= 100.0;
    }
    return numeric_confidence > 0.0 && numeric_confidence < 0.60;
  }
  const auto confidence = value.toString().trimmed().toLower();
  return confidence == QStringLiteral("low") ||
         confidence == QStringLiteral("uncertain") ||
         confidence == QStringLiteral("needs_review") ||
         confidence == QStringLiteral("needs review");
}

QVariantMap pre_draft_stop_policy(const QVariantMap& plan) {
  const auto risk = plan.value(QStringLiteral("riskAssessment")).toMap();
  QStringList triggers;
  if (risk.value(QStringLiteral("approvalRequired")).toBool()) {
    triggers.append(QStringLiteral("high_risk"));
  }
  QVariant confidence;
  for (const auto& key :
       {QStringLiteral("confidence"),
        QStringLiteral("modelConfidence"),
        QStringLiteral("confidenceScore")}) {
    const auto candidate = plan.value(key);
    if (candidate.isValid() && !candidate.isNull()) {
      confidence = candidate;
      break;
    }
  }
  if (confidence_value_is_low(confidence)) {
    triggers.append(QStringLiteral("low_confidence"));
  }
  return QVariantMap{
      {QStringLiteral("enabled"), true},
      {QStringLiteral("stopRequired"), !triggers.isEmpty()},
      {QStringLiteral("triggers"), triggers},
      {QStringLiteral("confidence"), confidence},
      {QStringLiteral("approvalTypes"), risk.value(QStringLiteral("approvalTypes"))},
      {QStringLiteral("highRiskRequiresApproval"), true},
      {QStringLiteral("lowConfidenceRequiresQuestion"), true},
      {QStringLiteral("rulebook"),
       QStringLiteral("Ask before creating model-planned drafts when confidence is low or app risk assessment requires approval.")},
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
  auto edited_count = 0;
  auto passed_count = 0;
  auto needs_review_count = 0;
  auto failed_count = 0;
  QVariantMap cluster_summaries_by_id;
  QStringList cluster_order;
  QVariantList review_issues;

  for (auto index = 0; index < asset_reports.size(); ++index) {
    const auto report = asset_reports.at(index).toMap();
    const auto status = report.value(QStringLiteral("status"), QStringLiteral("pending")).toString();
    status_counts.insert(status, status_counts.value(status).toInt() + 1);
    const auto warnings = report.value(QStringLiteral("warnings")).toStringList();
    const auto errors = report.value(QStringLiteral("errors")).toStringList();
    warning_count += warnings.size();
    error_count += errors.size();

    const auto validation_report = report.value(QStringLiteral("validationReport")).toMap();
    const auto style_match = validation_report.value(QStringLiteral("styleMatch")).toMap();
    const auto asset_id = report.value(
        QStringLiteral("assetId"),
        index < asset_ids.size() ? asset_ids.at(index) : QString()).toString();
    const auto validation_ok =
        !validation_report.contains(QStringLiteral("ok")) ||
        validation_report.value(QStringLiteral("ok")).toBool();
    const auto style_needs_review = style_match.value(QStringLiteral("needsReview")).toBool();
    const auto corrected_outlier =
        report.value(QStringLiteral("revisionCount")).toInt() > 0 ||
        report.value(QStringLiteral("validationIterationLimitReached")).toBool();
    const auto review_issue_resolved = report.value(QStringLiteral("reviewIssueResolved")).toBool();
    const auto asset_failed =
        status == QStringLiteral("failed") ||
        status == QStringLiteral("error") ||
        !validation_ok ||
        !errors.isEmpty();
    const auto asset_needs_review =
        !review_issue_resolved &&
        (asset_failed || !warnings.isEmpty() || style_needs_review || corrected_outlier);
    if (status == QStringLiteral("drafted") ||
        status == QStringLiteral("committed") ||
        status == QStringLiteral("prepared")) {
      ++edited_count;
    }
    if (asset_failed) {
      ++failed_count;
    } else if (asset_needs_review) {
      ++needs_review_count;
    } else if (status == QStringLiteral("drafted") ||
               status == QStringLiteral("committed") ||
               status == QStringLiteral("prepared")) {
      ++passed_count;
    }
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

    if (asset_needs_review) {
      QString reason;
      QString suggested_action;
      if (asset_failed) {
        reason = errors.isEmpty()
                     ? QStringLiteral("Validation failed for this photo.")
                     : errors.join(QStringLiteral("; "));
        suggested_action = QStringLiteral("Fix or roll back this photo before commit.");
      } else if (!warnings.isEmpty()) {
        reason = warnings.join(QStringLiteral("; "));
        suggested_action = QStringLiteral("Review the proof and adjust if the warning is visible.");
      } else if (style_needs_review) {
        reason = style_match.value(
            QStringLiteral("reason"),
            QStringLiteral("Rendered proof differs from the learned style.")).toString();
        suggested_action = QStringLiteral("Compare against the approved style samples.");
      } else {
        reason = QStringLiteral("Validation repaired this photo independently from its cluster.");
        suggested_action = QStringLiteral("Review the corrected outlier before commit.");
      }
      review_issues.append(QVariantMap{
          {QStringLiteral("assetId"), asset_id},
          {QStringLiteral("status"), asset_failed ? QStringLiteral("failed") : QStringLiteral("needs_review")},
          {QStringLiteral("reason"), reason},
          {QStringLiteral("suggestedAction"), suggested_action},
          {QStringLiteral("thumbnailPath"), artifacts.value(QStringLiteral("afterProofPath"))},
          {QStringLiteral("afterProofPath"), artifacts.value(QStringLiteral("afterProofPath"))},
          {QStringLiteral("clusterId"), cluster_id},
          {QStringLiteral("warningCount"), warnings.size()},
          {QStringLiteral("errorCount"), errors.size()},
          {QStringLiteral("styleMatchScorePercent"), style_match.value(QStringLiteral("scorePercent"))},
          {QStringLiteral("styleMatchNeedsReview"), style_needs_review},
          {QStringLiteral("outlierCorrected"), corrected_outlier},
          {QStringLiteral("actions"),
           QVariantList{
               QVariantMap{{QStringLiteral("key"), QStringLiteral("fix")},
                           {QStringLiteral("label"), QStringLiteral("Fix")}},
               QVariantMap{{QStringLiteral("key"), QStringLiteral("accept_anyway")},
                           {QStringLiteral("label"), QStringLiteral("Accept anyway")}},
               QVariantMap{{QStringLiteral("key"), QStringLiteral("rollback_photo")},
                           {QStringLiteral("label"), QStringLiteral("Rollback this photo")}},
           }},
      });
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
        {QStringLiteral("styleMatchScorePercent"), style_match.value(QStringLiteral("scorePercent"))},
        {QStringLiteral("styleMatchNeedsReview"), style_match.value(QStringLiteral("needsReview"))},
    });
  }

  QVariantList cluster_summaries;
  for (const auto& cluster_id : cluster_order) {
    cluster_summaries.append(cluster_summaries_by_id.value(cluster_id).toMap());
  }

  const auto asset_count = asset_ids.isEmpty() ? asset_reports.size() : asset_ids.size();
  const auto variant_families = variant_family_summaries(asset_reports);
  const QVariantMap review_summary{
      {QStringLiteral("editedCount"), edited_count},
      {QStringLiteral("passedCount"), passed_count},
      {QStringLiteral("needsReviewCount"), needs_review_count},
      {QStringLiteral("failedCount"), failed_count},
      {QStringLiteral("issueCount"), review_issues.size()},
      {QStringLiteral("issueAssetIds"),
       [&review_issues]() {
         QStringList ids;
         for (const auto& issue_value : review_issues) {
           const auto asset_id = issue_value.toMap().value(QStringLiteral("assetId")).toString();
           if (!asset_id.isEmpty()) {
             ids.append(asset_id);
           }
         }
         return ids;
       }()},
      {QStringLiteral("showOnlyIssuesFirst"), !review_issues.isEmpty()},
      {QStringLiteral("detailsCollapsedByDefault"), true},
      {QStringLiteral("fixAllOutliersAvailable"), needs_review_count > 1 && failed_count == 0},
  };
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
      {QStringLiteral("reviewSummary"), review_summary},
      {QStringLiteral("reviewIssues"), review_issues},
  };
}

QString sample_review_badge(const QVariantMap& asset_report) {
  const auto validation_report = asset_report.value(QStringLiteral("validationReport")).toMap();
  if (!validation_report.value(QStringLiteral("ok")).toBool()) {
    return QStringLiteral("fail");
  }
  if (!asset_report.value(QStringLiteral("warnings")).toStringList().isEmpty() ||
      !validation_report.value(QStringLiteral("warnings")).toStringList().isEmpty() ||
      validation_report.value(QStringLiteral("styleMatch")).toMap().value(QStringLiteral("needsReview")).toBool()) {
    return QStringLiteral("warn");
  }
  return QStringLiteral("pass");
}

QVariantMap sample_review_report(
    const QVariantList& asset_reports,
    const QStringList& representative_asset_ids,
    const QStringList& full_target_asset_ids,
    const QStringList& remaining_asset_ids,
    const QVariantList& cluster_plans,
    const QString& plan_summary) {
  QVariantMap reports_by_asset_id;
  for (const auto& report_value : asset_reports) {
    const auto report = report_value.toMap();
    const auto asset_id = report.value(QStringLiteral("assetId")).toString();
    if (!asset_id.isEmpty()) {
      reports_by_asset_id.insert(asset_id, report);
    }
  }
  QVariantMap cluster_plan_by_representative_id;
  for (const auto& plan_value : cluster_plans) {
    const auto plan = plan_value.toMap();
    const auto representative_id =
        plan.value(QStringLiteral("representativeAssetId")).toString();
    if (!representative_id.isEmpty()) {
      cluster_plan_by_representative_id.insert(representative_id, plan);
    }
  }

  QStringList sample_ids = representative_asset_ids;
  if (sample_ids.isEmpty() && asset_reports.size() == 1) {
    sample_ids.append(asset_reports.first().toMap().value(QStringLiteral("assetId")).toString());
  }
  QVariantList samples;
  for (const auto& asset_id : sample_ids) {
    const auto report = reports_by_asset_id.value(asset_id).toMap();
    if (report.isEmpty()) {
      continue;
    }
    const auto validation_report = report.value(QStringLiteral("validationReport")).toMap();
    const auto artifacts = validation_report.value(QStringLiteral("artifacts")).toMap().isEmpty()
                               ? report.value(QStringLiteral("artifacts")).toMap()
                               : validation_report.value(QStringLiteral("artifacts")).toMap();
    const auto cluster_plan = cluster_plan_by_representative_id.value(asset_id).toMap();
    const auto badge = sample_review_badge(report);
    samples.append(QVariantMap{
        {QStringLiteral("assetId"), asset_id},
        {QStringLiteral("clusterId"), report.value(QStringLiteral("clusterId"))},
        {QStringLiteral("clusterKey"), report.value(QStringLiteral("clusterKey"))},
        {QStringLiteral("representative"), true},
        {QStringLiteral("badge"), badge},
        {QStringLiteral("badgeLabel"),
         badge == QStringLiteral("pass")
             ? QStringLiteral("Pass")
             : badge == QStringLiteral("warn") ? QStringLiteral("Warn") : QStringLiteral("Fail")},
        {QStringLiteral("explanation"),
         cluster_plan.value(QStringLiteral("planSummary")).toString().trimmed().isEmpty()
             ? plan_summary
             : cluster_plan.value(QStringLiteral("planSummary")).toString()},
        {QStringLiteral("beforeProofPath"), artifacts.value(QStringLiteral("beforeProofPath"))},
        {QStringLiteral("afterProofPath"), artifacts.value(QStringLiteral("afterProofPath"))},
        {QStringLiteral("validationOk"), validation_report.value(QStringLiteral("ok"))},
        {QStringLiteral("warningCount"),
         report.value(QStringLiteral("warnings")).toStringList().size() +
             validation_report.value(QStringLiteral("warnings")).toStringList().size()},
        {QStringLiteral("styleMatchScorePercent"),
         validation_report.value(QStringLiteral("styleMatch")).toMap().value(QStringLiteral("scorePercent"))},
    });
  }

  return QVariantMap{
      {QStringLiteral("available"), !samples.isEmpty()},
      {QStringLiteral("sampleFirst"), true},
      {QStringLiteral("mode"), QStringLiteral("sample-review")},
      {QStringLiteral("sampleAssetIds"), sample_ids},
      {QStringLiteral("visibleAssetIds"), sample_ids},
      {QStringLiteral("fullTargetAssetIds"),
       full_target_asset_ids.isEmpty() ? sample_ids : full_target_asset_ids},
      {QStringLiteral("fullTargetCount"),
       full_target_asset_ids.isEmpty() ? sample_ids.size() : full_target_asset_ids.size()},
      {QStringLiteral("remainingAssetIds"), remaining_asset_ids},
      {QStringLiteral("remainingCount"), remaining_asset_ids.size()},
      {QStringLiteral("applyToAllAvailable"), !remaining_asset_ids.isEmpty()},
      {QStringLiteral("sampleCount"), samples.size()},
      {QStringLiteral("samples"), samples},
      {QStringLiteral("showOnlySamplesFirst"), true},
      {QStringLiteral("feedbackMode"), QStringLiteral("natural-language")},
      {QStringLiteral("reviewActions"),
       QVariantList{
           QVariantMap{{QStringLiteral("key"), QStringLiteral("looks_good")},
                       {QStringLiteral("label"), QStringLiteral("Looks good")}},
           QVariantMap{{QStringLiteral("key"), QStringLiteral("make_changes")},
                       {QStringLiteral("label"), QStringLiteral("Make changes")}},
           QVariantMap{{QStringLiteral("key"), QStringLiteral("reject")},
                       {QStringLiteral("label"), QStringLiteral("Reject")}},
           QVariantMap{{QStringLiteral("key"), QStringLiteral("apply_to_all")},
                       {QStringLiteral("label"), QStringLiteral("Apply to All")}},
       }},
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

QVariantMap batch_resume_plan(
    const QString& run_id,
    const QString& scope,
    const QVariantMap& default_patch,
    const QVariantList& asset_reports,
    const QVariantList& batch_clusters,
    const QVariant& cluster_planning,
    const QVariant& representative_asset_ids) {
  QStringList clean_asset_ids;
  QVariantMap per_asset_patches;
  QVariantList clean_asset_reports;
  for (const auto& report_value : asset_reports) {
    const auto report = report_value.toMap();
    const auto asset_id = report.value(QStringLiteral("assetId")).toString();
    const auto final_patch = report.value(QStringLiteral("finalRecipePatch")).toMap();
    if (asset_id.isEmpty() || final_patch.isEmpty() ||
        !report.value(QStringLiteral("validationReport")).toMap().value(QStringLiteral("ok")).toBool()) {
      continue;
    }
    clean_asset_ids.append(asset_id);
    per_asset_patches.insert(asset_id, final_patch);
    clean_asset_reports.append(report);
  }
  return QVariantMap{
      {QStringLiteral("available"), !clean_asset_ids.isEmpty()},
      {QStringLiteral("sourceRunId"), run_id},
      {QStringLiteral("scope"), scope},
      {QStringLiteral("resumeStrategy"), QStringLiteral("reuse-clean-asset-validation-and-continue")},
      {QStringLiteral("cleanAssetIds"), clean_asset_ids},
      {QStringLiteral("cleanAssetReports"), clean_asset_reports},
      {QStringLiteral("defaultRecipePatch"), default_patch},
      {QStringLiteral("perAssetPatches"), per_asset_patches},
      {QStringLiteral("batchClusters"), batch_clusters},
      {QStringLiteral("clusterPlanning"), cluster_planning},
      {QStringLiteral("representativeAssetIds"), representative_asset_ids},
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
      {QStringLiteral("userVisibleToolEvents"), report.value(QStringLiteral("userVisibleToolEvents"))},
      {QStringLiteral("toolEventDisplayPolicy"), report.value(QStringLiteral("toolEventDisplayPolicy"))},
      {QStringLiteral("userVisibleMemoryEvents"), report.value(QStringLiteral("userVisibleMemoryEvents"))},
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

QString inferred_agent_scope(const CatalogAssetModel* catalog_model) {
  if (catalog_model == nullptr) {
    return QStringLiteral("selected-assets");
  }
  if (catalog_model->selectedAssetCount() > 1) {
    return QStringLiteral("selected-assets");
  }
  const auto project_filter = catalog_model->projectFilter().trimmed();
  if (!project_filter.isEmpty() &&
      project_filter != QStringLiteral("all") &&
      !catalog_model->assetsForProject(project_filter).isEmpty()) {
    return QStringLiteral("project");
  }
  if (catalog_model->totalAssetCount() > 0 &&
      catalog_model->assetCount() > 0 &&
      catalog_model->assetCount() < catalog_model->totalAssetCount()) {
    return QStringLiteral("filtered-library");
  }
  if (catalog_model->selectedAssetCount() == 1 || catalog_model->selectedIndex() >= 0) {
    return QStringLiteral("current-photo");
  }
  return QStringLiteral("selected-assets");
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

QVariantMap active_project_scope_context(
    const CatalogAssetModel* catalog_model,
    const QString& requested_scope,
    const QVariantList& assets) {
  QVariantMap context{
      {QStringLiteral("requestedScope"), normalized_agent_scope(requested_scope)},
      {QStringLiteral("activeProjectRequired"), false},
      {QStringLiteral("outsideActiveProject"), false},
      {QStringLiteral("scopeEscapesActiveProject"), false},
      {QStringLiteral("outsideActiveProjectAssetIds"), QStringList{}},
  };
  if (catalog_model == nullptr) {
    return context;
  }
  const auto active_project = catalog_model->projectFilter().trimmed();
  if (active_project.isEmpty() ||
      active_project == QStringLiteral("all") ||
      active_project == QStringLiteral("unassigned")) {
    return context;
  }

  QStringList outside_asset_ids;
  for (const auto& asset_value : assets) {
    const auto asset = asset_value.toMap();
    const auto project_names = asset.value(QStringLiteral("projectNames")).toStringList();
    if (!project_names.contains(active_project)) {
      const auto asset_id = asset.value(QStringLiteral("assetId")).toString();
      if (!asset_id.isEmpty()) {
        outside_asset_ids.append(asset_id);
      }
    }
  }

  context.insert(QStringLiteral("activeProjectRequired"), true);
  context.insert(QStringLiteral("activeProjectName"), active_project);
  context.insert(QStringLiteral("activeProjectAssetCount"), catalog_model->assetCountForProject(active_project));
  context.insert(QStringLiteral("assetCount"), static_cast<int>(assets.size()));
  const auto normalized_scope = context.value(QStringLiteral("requestedScope")).toString();
  const auto scope_escapes_active_project =
      normalized_scope != QStringLiteral("project") &&
      normalized_scope != QStringLiteral("current-photo") &&
      normalized_scope != QStringLiteral("selected-assets");
  context.insert(QStringLiteral("scopeEscapesActiveProject"), scope_escapes_active_project);
  context.insert(
      QStringLiteral("outsideActiveProject"),
      !outside_asset_ids.isEmpty() || scope_escapes_active_project);
  context.insert(QStringLiteral("outsideActiveProjectAssetIds"), outside_asset_ids);
  return context;
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

QString ai_scope_label(const QString& scope, int asset_count) {
  const auto normalized_scope = normalized_agent_scope(scope);
  if (asset_count <= 0) {
    if (normalized_scope == QStringLiteral("project")) {
      return QStringLiteral("No project photos");
    }
    if (normalized_scope == QStringLiteral("filtered-library")) {
      return QStringLiteral("No filtered photos");
    }
    if (normalized_scope == QStringLiteral("current-photo")) {
      return QStringLiteral("No current photo");
    }
    return QStringLiteral("No selected photos");
  }
  if (asset_count == 1) {
    return QStringLiteral("1 photo");
  }
  if (normalized_scope == QStringLiteral("project")) {
    return QStringLiteral("%1 project photos").arg(asset_count);
  }
  if (normalized_scope == QStringLiteral("filtered-library")) {
    return QStringLiteral("%1 filtered photos").arg(asset_count);
  }
  if (normalized_scope == QStringLiteral("current-photo")) {
    return QStringLiteral("%1 current photos").arg(asset_count);
  }
  return QStringLiteral("%1 selected photos").arg(asset_count);
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

QStringList ordered_difference(const QStringList& values, const QStringList& excluded) {
  QSet<QString> excluded_set;
  for (const auto& value : excluded) {
    excluded_set.insert(value);
  }
  QStringList difference;
  for (const auto& value : values) {
    if (!value.isEmpty() && !excluded_set.contains(value)) {
      difference.append(value);
    }
  }
  return difference;
}

QVariantList filter_asset_maps_by_ids(const QVariantList& assets, const QStringList& allowed_ids) {
  if (allowed_ids.isEmpty()) {
    return assets;
  }
  QSet<QString> allowed_set;
  for (const auto& asset_id : allowed_ids) {
    allowed_set.insert(asset_id);
  }
  QVariantList filtered;
  for (const auto& asset_value : assets) {
    const auto asset = asset_value.toMap();
    if (allowed_set.contains(asset.value(QStringLiteral("assetId")).toString())) {
      filtered.append(asset);
    }
  }
  return filtered;
}

QVariantMap patches_by_asset_id_to_variant_map(
    const QHash<QString, QVariantMap>& patches_by_asset_id) {
  QVariantMap patches;
  for (auto iterator = patches_by_asset_id.cbegin(); iterator != patches_by_asset_id.cend(); ++iterator) {
    if (!iterator.key().isEmpty() && !iterator.value().isEmpty()) {
      patches.insert(iterator.key(), iterator.value());
    }
  }
  return patches;
}

QHash<QString, QVariantMap> patches_by_asset_id_from_variant_map(const QVariantMap& patches) {
  QHash<QString, QVariantMap> parsed;
  for (auto iterator = patches.cbegin(); iterator != patches.cend(); ++iterator) {
    const auto patch = iterator.value().toMap();
    if (!iterator.key().isEmpty() && !patch.isEmpty()) {
      parsed.insert(iterator.key(), patch);
    }
  }
  return parsed;
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

QVariantMap model_repair_recipe_patch_from_validation(
    const QVariantMap& repair_context,
    const QVariantMap& asset,
    const QString& current_recipe_json,
    const QVariantMap& current_patch,
    const QVariantMap& validation_report,
    const QVariantMap& style,
    const QVariantList& previous_attempts = {}) {
  QVariantMap report{
      {QStringLiteral("mode"), QStringLiteral("model-validation-repair")},
      {QStringLiteral("ok"), false},
      {QStringLiteral("modelId"), repair_context.value(QStringLiteral("modelId"))},
  };
  const auto model_id = repair_context.value(QStringLiteral("modelId")).toString().trimmed();
  if (model_id.isEmpty()) {
    report.insert(QStringLiteral("errors"), QStringList{QStringLiteral("No model repair context is available.")});
    return report;
  }
  const auto warnings = validation_report.value(QStringLiteral("warnings")).toStringList();
  if (warnings.isEmpty()) {
    report.insert(QStringLiteral("errors"), QStringList{QStringLiteral("No validation warnings were available for model repair.")});
    return report;
  }

  auto repair_asset = asset;
  const auto artifacts = validation_report.value(QStringLiteral("artifacts")).toMap();
  const auto after_proof_path = artifacts.value(QStringLiteral("afterProofPath")).toString();
  if (!after_proof_path.trimmed().isEmpty()) {
    repair_asset.insert(QStringLiteral("proofImagePath"), after_proof_path);
    repair_asset.insert(QStringLiteral("afterProofPath"), after_proof_path);
  }

  AgentLocalModelPlanRequest request;
  request.model_id = model_id;
  request.style = style;
  request.asset = repair_asset;
  request.current_recipe_json = current_recipe_json;
  request.allow_tool_call_plan = false;
  request.available_tool_schemas =
      repair_context.value(QStringLiteral("availableToolSchemas")).toList();
  request.strategy_phase =
      repair_context.value(QStringLiteral("toolPhase"), QStringLiteral("revision")).toString();
  request.memory_packet = repair_context.value(QStringLiteral("memoryPacket")).toMap();
  request.prior_tool_results = QVariantList{
      QVariantMap{
          {QStringLiteral("kind"), QStringLiteral("validation-repair")},
          {QStringLiteral("warnings"), warnings},
          {QStringLiteral("currentPatch"), current_patch},
          {QStringLiteral("validationReport"), validation_report},
          {QStringLiteral("previousAttempts"), previous_attempts},
      },
  };
  request.user_instructions =
      QStringLiteral(
          "%1\n\nRepair the current recipe patch using the validation report. "
          "Return a complete replacement recipePatch that still follows the style rulebook, "
          "reduces the reported warnings, and does not request tool calls.\n"
          "Current patch JSON:\n%2\n"
          "Validation warnings JSON:\n%3\n"
          "Validation report JSON:\n%4\n"
          "Previous attempts JSON:\n%5")
          .arg(
              repair_context.value(QStringLiteral("userInstructions")).toString(),
              QString::fromUtf8(QJsonDocument::fromVariant(current_patch).toJson(QJsonDocument::Compact)),
              QString::fromUtf8(QJsonDocument::fromVariant(warnings).toJson(QJsonDocument::Compact)),
              QString::fromUtf8(QJsonDocument::fromVariant(validation_report).toJson(QJsonDocument::Compact)),
              QString::fromUtf8(QJsonDocument::fromVariant(previous_attempts).toJson(QJsonDocument::Compact)));

  const auto worker_result = plan_agent_recipe_patch_on_worker(request);
  const auto plan = worker_result.plan;
  report.insert(QStringLiteral("uiEventPumpCount"), worker_result.ui_event_pump_count);
  report.insert(QStringLiteral("decision"), plan.decision);
  report.insert(QStringLiteral("planSummary"), plan.plan_summary);
  report.insert(QStringLiteral("modelPromptAudit"), plan.prompt_audit);
  report.insert(QStringLiteral("warnings"), plan.warnings);
  if (!plan.ok) {
    report.insert(QStringLiteral("errors"), QStringList{plan.error_message});
    return report;
  }
  if (plan.decision != QStringLiteral("recipe_patch")) {
    report.insert(QStringLiteral("errors"), QStringList{QStringLiteral("Model repair must return decision=recipe_patch.")});
    return report;
  }
  if (plan.style_constraint_citations.isEmpty()) {
    report.insert(QStringLiteral("errors"), QStringList{QStringLiteral("Model repair did not cite active style constraints.")});
    return report;
  }
  const auto patch_validation = validate_agent_recipe_patch(plan.recipe_patch, style);
  if (!patch_validation.valid) {
    report.insert(QStringLiteral("errors"), patch_validation.errors);
    report.insert(QStringLiteral("warnings"), patch_validation.warnings);
    return report;
  }
  report.insert(QStringLiteral("ok"), true);
  report.insert(QStringLiteral("recipePatch"), plan.recipe_patch);
  report.insert(QStringLiteral("styleConstraintCitations"), plan.style_constraint_citations);
  report.insert(QStringLiteral("patchValidationWarnings"), patch_validation.warnings);
  return report;
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
    if (map.contains(QStringLiteral("userPrompt"))) {
      redacted.insert(QStringLiteral("userPrompt"), QStringLiteral("<redacted-prompt>"));
    }
    if (map.contains(QStringLiteral("systemPrompt"))) {
      redacted.insert(QStringLiteral("systemPrompt"), QStringLiteral("<redacted-system-prompt>"));
    }
    if (map.contains(QStringLiteral("userPrompt")) ||
        map.contains(QStringLiteral("systemPrompt"))) {
      redacted.insert(QStringLiteral("promptTextRedacted"), true);
    }
    return redacted;
  }
  return value;
}

QVariantMap redacted_shareable_report(const QVariantMap& report) {
  return redacted_shareable_value(report).toMap();
}

QVariantMap shareable_prompt_audit(QVariantMap prompt_audit) {
  if (prompt_audit.isEmpty()) {
    return {};
  }
  if (prompt_audit.contains(QStringLiteral("userPrompt"))) {
    prompt_audit.insert(QStringLiteral("userPrompt"), QStringLiteral("<redacted-prompt>"));
  }
  if (prompt_audit.contains(QStringLiteral("systemPrompt"))) {
    prompt_audit.insert(QStringLiteral("systemPrompt"), QStringLiteral("<redacted-system-prompt>"));
  }
  prompt_audit.insert(QStringLiteral("promptTextRedacted"), true);
  return prompt_audit;
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

bool should_show_tool_call_by_default(const QString& name, const QString& status) {
  if (status != QStringLiteral("ok")) {
    return true;
  }
  if (name.startsWith(QStringLiteral("mcp."))) {
    return false;
  }
  static const QSet<QString> kDefaultVisibleToolCalls{
      QStringLiteral("plan_agent_recipe_patch"),
      QStringLiteral("validate_recipe_patch"),
      QStringLiteral("render_current_proof"),
      QStringLiteral("render_proof_and_validate"),
      QStringLiteral("model_repair_recipe_patch_from_validation"),
      QStringLiteral("apply_model_repaired_recipe_patch"),
      QStringLiteral("revise_recipe_patch_from_validation"),
      QStringLiteral("apply_recipe_patch_to_selected"),
      QStringLiteral("reuse_resumable_asset_validation"),
  };
  return kDefaultVisibleToolCalls.contains(name);
}

QString user_visible_tool_event_title(const QString& name, const QString& status) {
  if (name == QStringLiteral("plan_agent_recipe_patch")) {
    return QStringLiteral("Planned the edit");
  }
  if (name == QStringLiteral("validate_recipe_patch")) {
    return QStringLiteral("Checked the edit recipe");
  }
  if (name == QStringLiteral("render_current_proof") ||
      name == QStringLiteral("render_proof_and_validate")) {
    return QStringLiteral("Rendered and validated a proof");
  }
  if (name == QStringLiteral("model_repair_recipe_patch_from_validation") ||
      name == QStringLiteral("apply_model_repaired_recipe_patch") ||
      name == QStringLiteral("revise_recipe_patch_from_validation")) {
    return QStringLiteral("Adjusted the edit after validation");
  }
  if (name == QStringLiteral("apply_recipe_patch_to_selected")) {
    return QStringLiteral("Prepared the draft edits");
  }
  if (name == QStringLiteral("reuse_resumable_asset_validation")) {
    return QStringLiteral("Reused a clean prior validation");
  }
  if (name == QStringLiteral("mcp.tools/list")) {
    return QStringLiteral("Checked editor tool connection");
  }
  if (name.startsWith(QStringLiteral("mcp."))) {
    return QStringLiteral("Checked an editor tool");
  }
  if (status == QStringLiteral("canceled")) {
    return QStringLiteral("Stopped the run");
  }
  if (status == QStringLiteral("failed")) {
    return QStringLiteral("Tool step needs attention");
  }
  return name;
}

QString user_visible_tool_event_detail(const QString& name, const QVariantMap& outcome) {
  if (name == QStringLiteral("apply_recipe_patch_to_selected")) {
    const auto drafted_count = outcome.value(QStringLiteral("draftedCount")).toInt();
    const auto asset_count = outcome.value(QStringLiteral("assetCount")).toInt();
    const auto count = drafted_count > 0 ? drafted_count : asset_count;
    if (count > 0) {
      return QStringLiteral("%1 photo%2 prepared for review.")
          .arg(count)
          .arg(count == 1 ? QString() : QStringLiteral("s"));
    }
  }
  if (name == QStringLiteral("plan_agent_recipe_patch")) {
    const auto citation_count = outcome.value(QStringLiteral("citationCount")).toInt();
    if (citation_count > 0) {
      return QStringLiteral("Used %1 style citation%2.")
          .arg(citation_count)
          .arg(citation_count == 1 ? QString() : QStringLiteral("s"));
    }
  }
  const auto warning_count = outcome.value(QStringLiteral("warningCount")).toInt();
  const auto error_count = outcome.value(QStringLiteral("errorCount")).toInt();
  if (error_count > 0 || warning_count > 0) {
    return QStringLiteral("%1 warning%2, %3 error%4.")
        .arg(warning_count)
        .arg(warning_count == 1 ? QString() : QStringLiteral("s"))
        .arg(error_count)
        .arg(error_count == 1 ? QString() : QStringLiteral("s"));
  }
  const auto error = outcome.value(QStringLiteral("error")).toString().trimmed();
  if (!error.isEmpty()) {
    return error;
  }
  return {};
}

QVariantMap user_visible_tool_event_report(const QVariantList& tool_calls) {
  QVariantList events;
  auto hidden_count = 0;
  constexpr int kMaxDefaultToolEvents = 12;
  for (const auto& value : tool_calls) {
    const auto tool_call = value.toMap();
    const auto name = tool_call.value(QStringLiteral("name")).toString();
    const auto status = tool_call.value(QStringLiteral("status")).toString();
    const auto outcome = tool_call.value(QStringLiteral("outcome")).toMap();
    const auto visible = should_show_tool_call_by_default(name, status);
    if (!visible || events.size() >= kMaxDefaultToolEvents) {
      ++hidden_count;
      continue;
    }
    QVariantMap event{
        {QStringLiteral("sourceToolName"), name},
        {QStringLiteral("status"), status},
        {QStringLiteral("title"), user_visible_tool_event_title(name, status)},
        {QStringLiteral("detail"), user_visible_tool_event_detail(name, outcome)},
        {QStringLiteral("recordedAtUtc"), tool_call.value(QStringLiteral("recordedAtUtc"))},
    };
    if (outcome.contains(QStringLiteral("assetId"))) {
      event.insert(QStringLiteral("assetId"), outcome.value(QStringLiteral("assetId")));
    }
    if (outcome.contains(QStringLiteral("assetCount"))) {
      event.insert(QStringLiteral("assetCount"), outcome.value(QStringLiteral("assetCount")));
    }
    events.append(event);
  }
  return {
      {QStringLiteral("events"), events},
      {QStringLiteral("policy"),
       QVariantMap{
           {QStringLiteral("defaultField"), QStringLiteral("userVisibleToolEvents")},
           {QStringLiteral("fullAuditField"), QStringLiteral("toolCalls")},
           {QStringLiteral("totalToolCallCount"), static_cast<int>(tool_calls.size())},
           {QStringLiteral("visibleToolEventCount"), static_cast<int>(events.size())},
           {QStringLiteral("hiddenToolCallCount"), hidden_count},
           {QStringLiteral("defaultHidesInternalMcpEvents"), true},
       }},
  };
}

QVariantMap user_visible_memory_event(
    const QString& change_type,
    const QString& style_id,
    const QString& style_name,
    const QString& title,
    const QString& detail,
    const QVariantMap& note_paths = {}) {
  QVariantMap event{
      {QStringLiteral("type"), QStringLiteral("durable-style-memory")},
      {QStringLiteral("changeType"), change_type},
      {QStringLiteral("styleId"), style_id},
      {QStringLiteral("styleName"), style_name},
      {QStringLiteral("title"), title},
      {QStringLiteral("detail"), detail},
      {QStringLiteral("requiresUserVisibility"), true},
      {QStringLiteral("recordedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
  };
  if (!note_paths.isEmpty()) {
    event.insert(QStringLiteral("notePaths"), note_paths);
  }
  return event;
}

QString style_change_summary_from_feedback(const QString& feedback) {
  return QStringLiteral("Style changed because: %1").arg(feedback.simplified());
}

QVariantMap parse_ollama_pull_progress(const QString& output) {
  QVariantMap progress;
  auto json_line_count = 0;
  auto line_count = 0;
  const auto lines = output.split(QLatin1Char('\n'));
  for (const auto& line : lines) {
    const auto trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
      continue;
    }
    ++line_count;
    progress.insert(QStringLiteral("latestLine"), trimmed);
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(trimmed.toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
      continue;
    }
    ++json_line_count;
    const auto object = document.object();
    const auto status = object.value(QStringLiteral("status")).toString().trimmed();
    if (!status.isEmpty()) {
      progress.insert(QStringLiteral("status"), status);
    }
    const auto digest = object.value(QStringLiteral("digest")).toString().trimmed();
    if (!digest.isEmpty()) {
      progress.insert(QStringLiteral("digest"), digest);
    }
    const auto total = object.value(QStringLiteral("total")).toVariant().toLongLong();
    const auto completed = object.value(QStringLiteral("completed")).toVariant().toLongLong();
    if (total > 0) {
      progress.insert(QStringLiteral("totalBytes"), total);
    }
    if (completed > 0) {
      progress.insert(QStringLiteral("completedBytes"), completed);
    }
    if (total > 0 && completed >= 0) {
      const auto fraction =
          std::clamp(static_cast<double>(completed) / static_cast<double>(total), 0.0, 1.0);
      progress.insert(QStringLiteral("progress"), fraction);
      progress.insert(QStringLiteral("progressPercent"), qRound(fraction * 100.0));
    }
  }
  if (line_count > 0) {
    progress.insert(QStringLiteral("lineCount"), line_count);
  }
  if (json_line_count > 0) {
    progress.insert(QStringLiteral("jsonLineCount"), json_line_count);
  }
  return progress;
}

QString model_install_status_from_progress(
    const QVariantMap& progress,
    const QString& fallback_status) {
  const auto status = progress.value(QStringLiteral("status")).toString().trimmed();
  if (status.isEmpty()) {
    return fallback_status;
  }
  const auto percent = progress.value(QStringLiteral("progressPercent"), -1).toInt();
  if (percent >= 0) {
    return QStringLiteral("%1 %2%").arg(status, QString::number(percent));
  }
  return status;
}

QStringList read_only_mcp_observation_tools() {
  return {
      QStringLiteral("photo.live_status"),
      QStringLiteral("photo.get_style_rulebook"),
      QStringLiteral("photo.get_recipe_patch_schema"),
  };
}

QString normalized_agent_phase(const QString& phase) {
  const auto normalized = phase.trimmed().toLower().replace(QLatin1Char('_'), QLatin1Char('-'));
  if (normalized == QStringLiteral("setup") ||
      normalized == QStringLiteral("style") ||
      normalized == QStringLiteral("sample") ||
      normalized == QStringLiteral("revision") ||
      normalized == QStringLiteral("batch") ||
      normalized == QStringLiteral("outlier") ||
      normalized == QStringLiteral("commit-export")) {
    return normalized;
  }
  if (normalized == QStringLiteral("commit") || normalized == QStringLiteral("export")) {
    return QStringLiteral("commit-export");
  }
  return QStringLiteral("sample");
}

QStringList mcp_tools_for_agent_phase(const QString& phase) {
  const auto normalized = normalized_agent_phase(phase);
  if (normalized == QStringLiteral("setup")) {
    return {
        QStringLiteral("photo.live_status"),
        QStringLiteral("photo.list_models"),
    };
  }
  if (normalized == QStringLiteral("style")) {
    return {
        QStringLiteral("photo.live_status"),
        QStringLiteral("photo.selected_assets"),
        QStringLiteral("photo.read_asset"),
        QStringLiteral("photo.get_style_rulebook"),
        QStringLiteral("photo.get_recipe_patch_schema"),
    };
  }
  if (normalized == QStringLiteral("revision")) {
    return {
        QStringLiteral("photo.live_status"),
        QStringLiteral("photo.read_asset"),
        QStringLiteral("photo.read_recipe"),
        QStringLiteral("photo.render_current_proof"),
        QStringLiteral("photo.get_style_rulebook"),
        QStringLiteral("photo.get_recipe_patch_schema"),
        QStringLiteral("photo.validate_recipe_patch"),
        QStringLiteral("photo.validate_edit"),
    };
  }
  if (normalized == QStringLiteral("batch")) {
    return {
        QStringLiteral("photo.live_status"),
        QStringLiteral("photo.selected_assets"),
        QStringLiteral("photo.read_asset"),
        QStringLiteral("photo.read_recipe"),
        QStringLiteral("photo.render_current_proof"),
        QStringLiteral("photo.get_style_rulebook"),
        QStringLiteral("photo.get_recipe_patch_schema"),
        QStringLiteral("photo.validate_recipe_patch"),
        QStringLiteral("photo.plan_bulk_edit"),
        QStringLiteral("photo.validate_batch"),
    };
  }
  if (normalized == QStringLiteral("outlier")) {
    return {
        QStringLiteral("photo.live_status"),
        QStringLiteral("photo.read_asset"),
        QStringLiteral("photo.read_recipe"),
        QStringLiteral("photo.render_current_proof"),
        QStringLiteral("photo.get_style_rulebook"),
        QStringLiteral("photo.get_recipe_patch_schema"),
        QStringLiteral("photo.validate_recipe_patch"),
        QStringLiteral("photo.validate_edit"),
        QStringLiteral("photo.rollback_agent_draft"),
    };
  }
  if (normalized == QStringLiteral("commit-export")) {
    return {
        QStringLiteral("photo.live_status"),
        QStringLiteral("photo.validate_batch"),
        QStringLiteral("photo.export_preflight"),
        QStringLiteral("photo.commit_agent_draft"),
        QStringLiteral("photo.export_batch"),
    };
  }
  return {
      QStringLiteral("photo.live_status"),
      QStringLiteral("photo.selected_assets"),
      QStringLiteral("photo.read_asset"),
      QStringLiteral("photo.read_recipe"),
      QStringLiteral("photo.render_current_proof"),
      QStringLiteral("photo.get_style_rulebook"),
      QStringLiteral("photo.get_recipe_patch_schema"),
      QStringLiteral("photo.validate_recipe_patch"),
      QStringLiteral("photo.validate_edit"),
  };
}

QVariantList filter_mcp_tools_for_phase(const QVariantList& tools, const QString& phase) {
  const auto allowed_tools = mcp_tools_for_agent_phase(phase);
  QVariantList filtered;
  for (const auto& tool_value : tools) {
    const auto tool = tool_value.toMap();
    const auto name = tool.value(QStringLiteral("name")).toString();
    if (allowed_tools.contains(name)) {
      filtered.append(tool);
    }
  }
  return filtered;
}

QVariantMap mcp_tool_budget_report(
    const QString& phase,
    const QStringList& allowed_tool_names,
    int available_tool_count) {
  return {
      {QStringLiteral("phase"), normalized_agent_phase(phase)},
      {QStringLiteral("maxModelToolCallsPerTurn"), kMaxModelToolCallsPerTurn},
      {QStringLiteral("allowedToolCount"), static_cast<int>(allowed_tool_names.size())},
      {QStringLiteral("availableToolCount"), available_tool_count},
      {QStringLiteral("hardLimit"), true},
      {QStringLiteral("budgetPolicy"), QStringLiteral("phase-allowlist-plus-per-turn-call-cap")},
  };
}

QStringList sorted_mcp_tool_names_from_schemas(const QVariantList& tools) {
  QStringList names;
  for (const auto& tool_value : tools) {
    const auto name = tool_value.toMap().value(QStringLiteral("name")).toString().trimmed();
    if (!name.isEmpty() && !names.contains(name)) {
      names.append(name);
    }
  }
  std::sort(names.begin(), names.end());
  return names;
}

QString mcp_tool_schema_fingerprint(const QVariantList& tools) {
  QStringList canonical_tools;
  for (const auto& tool_value : tools) {
    const auto tool = tool_value.toMap();
    const auto name = tool.value(QStringLiteral("name")).toString();
    const auto payload = QString::fromUtf8(
        QJsonDocument(QJsonObject::fromVariantMap(tool)).toJson(QJsonDocument::Compact));
    canonical_tools.append(QStringLiteral("%1\n%2").arg(name, payload));
  }
  std::sort(canonical_tools.begin(), canonical_tools.end());
  const auto joined = canonical_tools.join(QStringLiteral("\n---tool---\n")).toUtf8();
  return QString::fromLatin1(
      QCryptographicHash::hash(joined, QCryptographicHash::Sha256).toHex());
}

QStringList string_list_difference(const QStringList& next, const QStringList& previous) {
  QStringList difference;
  for (const auto& value : next) {
    if (!previous.contains(value)) {
      difference.append(value);
    }
  }
  return difference;
}

QVariantMap mcp_schema_cache_report(
    int revision,
    const QString& fingerprint,
    bool primed_before_refresh,
    bool changed,
    const QStringList& added_tool_names,
    const QStringList& removed_tool_names,
    int cached_tool_count) {
  return {
      {QStringLiteral("enabled"), true},
      {QStringLiteral("revision"), revision},
      {QStringLiteral("fingerprint"), fingerprint},
      {QStringLiteral("primedBeforeRefresh"), primed_before_refresh},
      {QStringLiteral("changed"), changed},
      {QStringLiteral("addedToolNames"), added_tool_names},
      {QStringLiteral("removedToolNames"), removed_tool_names},
      {QStringLiteral("cachedToolCount"), cached_tool_count},
      {QStringLiteral("schemaSource"), QStringLiteral("cached-after-refresh")},
  };
}

QVariantMap compact_mcp_tool_report(const QString& tool_name, const QVariantMap& call_report) {
  QVariantMap report{
      {QStringLiteral("toolName"), tool_name},
      {QStringLiteral("ok"), call_report.value(QStringLiteral("ok")).toBool()},
  };
  if (call_report.contains(QStringLiteral("error"))) {
    report.insert(QStringLiteral("error"), call_report.value(QStringLiteral("error")));
  }
  const auto structured_content = call_report.value(QStringLiteral("structuredContent")).toMap();
  if (!structured_content.isEmpty()) {
    report.insert(QStringLiteral("structuredContent"), structured_content);
    report.insert(QStringLiteral("structuredKeys"), structured_content.keys());
  }
  const auto content = call_report.value(QStringLiteral("content")).toList();
  if (!content.isEmpty()) {
    report.insert(QStringLiteral("content"), content);
  }
  return report;
}

QVariantMap execute_model_mcp_tool_plan(
    const QString& endpoint,
    const QString& endpoint_kind,
    const QString& run_id,
    const QStringList& allowed_tools,
    const QVariantList& cached_tools,
    const QVariantList& requested_tool_calls,
    const AgentCancellationCallback& cancellation_requested) {
  const auto effective_allowed_tools =
      allowed_tools.isEmpty() ? read_only_mcp_observation_tools() : allowed_tools;
  QVariantMap report{
      {QStringLiteral("mode"), QStringLiteral("mcp-model-tool-plan")},
      {QStringLiteral("endpoint"), endpoint},
      {QStringLiteral("endpointKind"), endpoint_kind},
      {QStringLiteral("runId"), run_id},
      {QStringLiteral("allowedTools"), effective_allowed_tools},
      {QStringLiteral("toolBudget"),
       QVariantMap{
           {QStringLiteral("maxModelToolCallsPerTurn"), kMaxModelToolCallsPerTurn},
           {QStringLiteral("requestedToolCallCount"), static_cast<int>(requested_tool_calls.size())},
           {QStringLiteral("allowedToolCount"), static_cast<int>(effective_allowed_tools.size())},
           {QStringLiteral("hardLimit"), true},
       }},
  };
  if (endpoint.trimmed().isEmpty()) {
    report.insert(QStringLiteral("ok"), false);
    report.insert(QStringLiteral("status"), QStringLiteral("unavailable"));
    report.insert(QStringLiteral("errors"), QStringList{QStringLiteral("MCP endpoint is not available.")});
    report.insert(QStringLiteral("toolReports"), QVariantList{});
    return report;
  }
  if (requested_tool_calls.isEmpty()) {
    report.insert(QStringLiteral("ok"), false);
    report.insert(QStringLiteral("status"), QStringLiteral("empty"));
    report.insert(QStringLiteral("errors"), QStringList{QStringLiteral("Model did not request any MCP tool calls.")});
    report.insert(QStringLiteral("toolReports"), QVariantList{});
    return report;
  }

  AgentMcpClient client;
  client.setEndpoint(endpoint);
  client.setTimeoutMs(3000);
  client.setAllowedTools(effective_allowed_tools);
  client.setCancellationPredicate(cancellation_requested);
  const auto tools_report = client.refreshToolCache(run_id);
  report.insert(QStringLiteral("toolCache"), tools_report);
  if (!tools_report.value(QStringLiteral("ok")).toBool()) {
    report.insert(QStringLiteral("ok"), false);
    report.insert(
        QStringLiteral("status"),
        tools_report.value(QStringLiteral("canceled")).toBool()
            ? QStringLiteral("canceled")
            : QStringLiteral("failed"));
    report.insert(QStringLiteral("canceled"), tools_report.value(QStringLiteral("canceled")));
    report.insert(QStringLiteral("errors"), QStringList{tools_report.value(QStringLiteral("error")).toString()});
    report.insert(QStringLiteral("toolReports"), QVariantList{});
    return report;
  }

  QVariantList tool_reports;
  QStringList errors;
  auto success_count = 0;
  auto attempted_count = 0;
  for (const auto& request_value : requested_tool_calls) {
    if (cancellation_requested && cancellation_requested()) {
      errors.append(QStringLiteral("MCP model tool-call execution was canceled."));
      break;
    }
    if (attempted_count >= kMaxModelToolCallsPerTurn) {
      errors.append(QStringLiteral("Model tool-call plan exceeded the per-turn limit."));
      break;
    }
    const auto request = request_value.toMap();
    const auto tool_name = request.value(QStringLiteral("name")).toString().trimmed();
    const auto arguments = request.value(QStringLiteral("arguments")).toMap();
    if (tool_name.isEmpty()) {
      errors.append(QStringLiteral("Model requested a tool call without a name."));
      continue;
    }
    ++attempted_count;
    if (!effective_allowed_tools.contains(tool_name)) {
      errors.append(QStringLiteral("Model requested a tool outside the active phase allowlist: %1").arg(tool_name));
      tool_reports.append(QVariantMap{
          {QStringLiteral("toolName"), tool_name},
          {QStringLiteral("ok"), false},
          {QStringLiteral("blockedByAllowlist"), true},
      });
      continue;
    }
    if (!client.hasCachedTool(tool_name)) {
      errors.append(QStringLiteral("MCP tool is missing from cache: %1").arg(tool_name));
      tool_reports.append(QVariantMap{
          {QStringLiteral("toolName"), tool_name},
          {QStringLiteral("ok"), false},
          {QStringLiteral("missingFromToolCache"), true},
      });
      continue;
    }
    const auto call_report = client.callTool(tool_name, arguments);
    auto compact_report = compact_mcp_tool_report(tool_name, call_report);
    compact_report.insert(QStringLiteral("reason"), request.value(QStringLiteral("reason")));
    tool_reports.append(compact_report);
    if (compact_report.value(QStringLiteral("ok")).toBool()) {
      ++success_count;
    } else {
      errors.append(call_report.value(QStringLiteral("error")).toString());
    }
  }
  const auto canceled = cancellation_requested && cancellation_requested();
  report.insert(QStringLiteral("ok"), success_count > 0 && errors.isEmpty() && !canceled);
  report.insert(
      QStringLiteral("status"),
      canceled ? QStringLiteral("canceled") : errors.isEmpty() ? QStringLiteral("ok") : QStringLiteral("partial"));
  report.insert(QStringLiteral("canceled"), canceled);
  report.insert(QStringLiteral("attemptedCount"), attempted_count);
  report.insert(QStringLiteral("successCount"), success_count);
  report.insert(QStringLiteral("requestedToolCalls"), requested_tool_calls);
  report.insert(QStringLiteral("toolReports"), tool_reports);
  report.insert(QStringLiteral("errors"), errors);
  report.insert(QStringLiteral("cachedTools"), cached_tools);
  return report;
}

QVariantMap observe_mcp_tools(
    const QString& endpoint,
    const QString& endpoint_kind,
    const QString& run_id,
    const AgentCancellationCallback& cancellation_requested) {
  QVariantMap report{
      {QStringLiteral("mode"), QStringLiteral("mcp-observation")},
      {QStringLiteral("endpoint"), endpoint},
      {QStringLiteral("endpointKind"), endpoint_kind},
      {QStringLiteral("runId"), run_id},
      {QStringLiteral("allowedTools"), read_only_mcp_observation_tools()},
  };
  if (endpoint.trimmed().isEmpty()) {
    report.insert(QStringLiteral("ok"), false);
    report.insert(QStringLiteral("status"), QStringLiteral("unavailable"));
    report.insert(QStringLiteral("errors"), QStringList{QStringLiteral("MCP endpoint is not available.")});
    report.insert(QStringLiteral("toolReports"), QVariantList{});
    return report;
  }

  AgentMcpClient client;
  client.setEndpoint(endpoint);
  client.setTimeoutMs(3000);
  client.setAllowedTools(read_only_mcp_observation_tools());
  client.setCancellationPredicate(cancellation_requested);
  const auto tools_report = client.refreshToolCache(run_id);
  report.insert(QStringLiteral("toolCache"), tools_report);
  if (!tools_report.value(QStringLiteral("ok")).toBool()) {
    report.insert(QStringLiteral("ok"), false);
    report.insert(
        QStringLiteral("status"),
        tools_report.value(QStringLiteral("canceled")).toBool()
            ? QStringLiteral("canceled")
            : QStringLiteral("failed"));
    report.insert(QStringLiteral("canceled"), tools_report.value(QStringLiteral("canceled")));
    report.insert(QStringLiteral("errors"), QStringList{tools_report.value(QStringLiteral("error")).toString()});
    report.insert(QStringLiteral("toolReports"), QVariantList{});
    return report;
  }

  QVariantList tool_reports;
  QStringList errors;
  auto success_count = 0;
  for (const auto& tool_name : read_only_mcp_observation_tools()) {
    if (cancellation_requested && cancellation_requested()) {
      errors.append(QStringLiteral("MCP observation was canceled."));
      break;
    }
    if (!client.hasCachedTool(tool_name)) {
      errors.append(QStringLiteral("MCP tool is missing from cache: %1").arg(tool_name));
      tool_reports.append(QVariantMap{
          {QStringLiteral("toolName"), tool_name},
          {QStringLiteral("ok"), false},
          {QStringLiteral("missingFromToolCache"), true},
      });
      continue;
    }
    const auto call_report = client.callTool(tool_name);
    const auto compact_report = compact_mcp_tool_report(tool_name, call_report);
    tool_reports.append(compact_report);
    if (compact_report.value(QStringLiteral("ok")).toBool()) {
      ++success_count;
    } else {
      errors.append(call_report.value(QStringLiteral("error")).toString());
    }
  }
  const auto canceled = cancellation_requested && cancellation_requested();
  report.insert(QStringLiteral("ok"), success_count > 0 && errors.isEmpty() && !canceled);
  report.insert(
      QStringLiteral("status"),
      canceled ? QStringLiteral("canceled") : errors.isEmpty() ? QStringLiteral("ok") : QStringLiteral("partial"));
  report.insert(QStringLiteral("canceled"), canceled);
  report.insert(QStringLiteral("successCount"), success_count);
  report.insert(QStringLiteral("toolReports"), tool_reports);
  report.insert(QStringLiteral("errors"), errors);
  return report;
}

QVariantList mcp_observation_tool_call_records(const QVariantMap& observation) {
  QVariantList records;
  for (const auto& value : observation.value(QStringLiteral("toolReports")).toList()) {
    const auto tool_report = value.toMap();
    const auto tool_name = tool_report.value(QStringLiteral("toolName")).toString();
    if (tool_name.isEmpty()) {
      continue;
    }
    records.append(tool_call_record(
        QStringLiteral("mcp.tools/call %1").arg(tool_name),
        tool_report.value(QStringLiteral("ok")).toBool() ? QStringLiteral("ok") : QStringLiteral("failed"),
        QVariantMap{
            {QStringLiteral("toolName"), tool_name},
            {QStringLiteral("structuredKeys"), tool_report.value(QStringLiteral("structuredKeys"))},
            {QStringLiteral("missingFromToolCache"), tool_report.value(QStringLiteral("missingFromToolCache"))},
            {QStringLiteral("error"), tool_report.value(QStringLiteral("error"))},
        }));
  }
  return records;
}

QVariantList mcp_model_tool_plan_call_records(const QVariantMap& tool_plan) {
  QVariantList records;
  for (const auto& value : tool_plan.value(QStringLiteral("toolReports")).toList()) {
    const auto tool_report = value.toMap();
    const auto tool_name = tool_report.value(QStringLiteral("toolName")).toString();
    if (tool_name.isEmpty()) {
      continue;
    }
    records.append(tool_call_record(
        QStringLiteral("mcp.model_tool_call %1").arg(tool_name),
        tool_report.value(QStringLiteral("ok")).toBool() ? QStringLiteral("ok") : QStringLiteral("failed"),
        QVariantMap{
            {QStringLiteral("toolName"), tool_name},
            {QStringLiteral("structuredKeys"), tool_report.value(QStringLiteral("structuredKeys"))},
            {QStringLiteral("blockedByAllowlist"), tool_report.value(QStringLiteral("blockedByAllowlist"))},
            {QStringLiteral("missingFromToolCache"), tool_report.value(QStringLiteral("missingFromToolCache"))},
            {QStringLiteral("error"), tool_report.value(QStringLiteral("error"))},
            {QStringLiteral("reason"), tool_report.value(QStringLiteral("reason"))},
        }));
  }
  return records;
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
  const auto visible_tool_event_report = user_visible_tool_event_report(tool_calls);
  report->insert(
      QStringLiteral("userVisibleToolEvents"),
      visible_tool_event_report.value(QStringLiteral("events")));
  report->insert(
      QStringLiteral("toolEventDisplayPolicy"),
      visible_tool_event_report.value(QStringLiteral("policy")));
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
      {QStringLiteral("modelPromptAudit"), report->value(QStringLiteral("modelPromptAudit"))},
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
      {QStringLiteral("resumePlan"), report->value(QStringLiteral("resumePlan"))},
      {QStringLiteral("resumed"), report->value(QStringLiteral("resumed"))},
      {QStringLiteral("resumedFromRunId"), report->value(QStringLiteral("resumedFromRunId"))},
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
      {QStringLiteral("userVisibleToolEvents"), report->value(QStringLiteral("userVisibleToolEvents"))},
      {QStringLiteral("toolEventDisplayPolicy"), report->value(QStringLiteral("toolEventDisplayPolicy"))},
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
  shareable_manifest.insert(
      QStringLiteral("modelPromptAudit"),
      shareable_prompt_audit(shareable_manifest.value(QStringLiteral("modelPromptAudit")).toMap()));
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
  auto validation_report = validate_agent_edit_image(rendered, state, style);
  const auto style_match =
      style_match_report_for_validation(validation_report, recipe_analysis_summary(state, QString()), style);
  validation_report.insert(QStringLiteral("styleMatch"), style_match);
  if (style_match.value(QStringLiteral("needsReview")).toBool()) {
    auto validation_warnings = validation_report.value(QStringLiteral("warnings")).toStringList();
    validation_warnings.append(QStringLiteral("style-match review recommended: score %1%")
                                   .arg(style_match.value(QStringLiteral("scorePercent")).toInt()));
    validation_report.insert(QStringLiteral("warnings"), validation_warnings);
  }
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
  const auto style_match =
      style_match_report_for_validation(report, recipe_analysis_summary(after_state, QString()), style);
  report.insert(QStringLiteral("styleMatch"), style_match);
  if (style_match.value(QStringLiteral("needsReview")).toBool()) {
    auto style_match_warnings = report.value(QStringLiteral("warnings")).toStringList();
    style_match_warnings.append(QStringLiteral("style-match review recommended: score %1%")
                                    .arg(style_match.value(QStringLiteral("scorePercent")).toInt()));
    report.insert(QStringLiteral("warnings"), style_match_warnings);
  }
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
  refreshMemoryVaultSnapshot();
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

QString AgentEditController::mcpEndpoint() const {
  return mcp_endpoint_;
}

QString AgentEditController::mcpEndpointKind() const {
  return mcp_endpoint_kind_;
}

QString AgentEditController::mcpStatus() const {
  return mcp_status_;
}

QString AgentEditController::mcpLastError() const {
  return mcp_last_error_;
}

int AgentEditController::mcpToolCount() const {
  return static_cast<int>(mcp_tool_names_.size());
}

QStringList AgentEditController::mcpToolNames() const {
  return mcp_tool_names_;
}

bool AgentEditController::mcpAvailable() const {
  return mcp_available_;
}

int AgentEditController::mcpConnectedClientCount() const {
  return mcp_connected_client_count_;
}

int AgentEditController::mcpTotalClientCount() const {
  return mcp_total_client_count_;
}

QString AgentEditController::mcpLastToolCallName() const {
  return mcp_last_tool_call_name_;
}

QString AgentEditController::mcpLastToolCallStatus() const {
  return mcp_last_tool_call_status_;
}

QString AgentEditController::mcpLastToolCallAt() const {
  return mcp_last_tool_call_at_;
}

bool AgentEditController::modelInstallRunning() const {
  return model_install_process_ != nullptr;
}

QString AgentEditController::modelInstallStatus() const {
  return model_install_status_;
}

QString AgentEditController::modelInstallOutput() const {
  return model_install_output_;
}

QVariantMap AgentEditController::modelInstallProgress() const {
  return model_install_progress_;
}

QVariantMap AgentEditController::lastRunReport() const { return last_run_report_; }

QVariantList AgentEditController::runHistory() const { return run_history_; }

bool AgentEditController::awaitingUserInput() const {
  return pending_agent_input_.value(QStringLiteral("needsUserInput")).toBool();
}

QVariantList AgentEditController::pendingUserQuestions() const {
  return pending_agent_input_.value(QStringLiteral("userQuestions")).toList();
}

QString AgentEditController::safeStopReason() const {
  return pending_agent_input_.value(QStringLiteral("safeStopReason")).toString();
}

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

void AgentEditController::setMcpEndpoint(const QString& endpoint, const QString& endpoint_kind) {
  const auto trimmed_endpoint = endpoint.trimmed();
  const auto trimmed_kind = endpoint_kind.trimmed();
  const auto changed = mcp_endpoint_ != trimmed_endpoint || mcp_endpoint_kind_ != trimmed_kind;
  mcp_endpoint_ = trimmed_endpoint;
  mcp_endpoint_kind_ = trimmed_kind;
  if (changed) {
    mcp_tools_.clear();
    mcp_tool_schema_fingerprint_.clear();
    mcp_tool_schema_revision_ = 0;
    mcp_tool_schema_changed_ = false;
    mcp_added_tool_names_.clear();
    mcp_removed_tool_names_.clear();
  }
  if (mcp_endpoint_.isEmpty()) {
    setMcpTelemetryState(0, 0, {}, {}, {});
    setMcpStatusState(QStringLiteral("Unavailable"), {}, false);
    return;
  }
  if (changed) {
    setMcpStatusState(QStringLiteral("Checking"), {}, false);
  }
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

void AgentEditController::reloadModels() {
  available_models_ = available_agent_local_models();
  if (!selected_model_id_.isEmpty() &&
      model_metadata_by_id(available_models_, selected_model_id_).isEmpty()) {
    selected_model_id_ = default_agent_local_model_id();
    emit selectedModelChanged();
  }
  emit availableModelsChanged();
}

QVariantMap AgentEditController::refreshMcpTools() {
  QVariantMap report;
  report.insert(QStringLiteral("mode"), QStringLiteral("mcp-status"));
  report.insert(QStringLiteral("endpoint"), mcp_endpoint_);
  report.insert(QStringLiteral("endpointKind"), mcp_endpoint_kind_);
  const auto previous_fingerprint = mcp_tool_schema_fingerprint_;
  const auto previous_tool_names = sorted_mcp_tool_names_from_schemas(mcp_tools_);
  const auto was_primed = !previous_fingerprint.isEmpty();
  if (mcp_endpoint_.isEmpty()) {
    setMcpStatusState(QStringLiteral("Unavailable"), {}, false);
    mcp_tools_.clear();
    mcp_tool_schema_fingerprint_.clear();
    mcp_tool_schema_revision_ = 0;
    mcp_tool_schema_changed_ = false;
    mcp_added_tool_names_.clear();
    mcp_removed_tool_names_.clear();
    report.insert(QStringLiteral("ok"), false);
    report.insert(QStringLiteral("errors"), QStringList{QStringLiteral("MCP endpoint is not available.")});
    report.insert(QStringLiteral("available"), false);
    report.insert(QStringLiteral("status"), mcp_status_);
    report.insert(QStringLiteral("toolCount"), 0);
    report.insert(QStringLiteral("toolNames"), QStringList{});
    report.insert(
        QStringLiteral("schemaCache"),
        mcp_schema_cache_report(
            mcp_tool_schema_revision_,
            mcp_tool_schema_fingerprint_,
            was_primed,
            false,
            {},
            previous_tool_names,
            0));
    return report;
  }

  if (cancellation_requested_ && !running_) {
    cancellation_requested_ = false;
    rollback_on_cancel_ = false;
    cancellation_reason_.clear();
    emit cancellationChanged();
  }

  AgentMcpClient client;
  client.setEndpoint(mcp_endpoint_);
  client.setTimeoutMs(3000);
  client.setCancellationPredicate([this]() { return cancellation_requested_; });
  const auto tools_report = client.refreshToolCache(QStringLiteral("agent-ui-status"));
  if (!tools_report.value(QStringLiteral("ok")).toBool()) {
    const auto error = tools_report.value(QStringLiteral("error")).toString();
    setMcpStatusState(
        QStringLiteral("Failed"),
        {},
        false,
        error.isEmpty() ? QStringLiteral("Unable to refresh MCP tool schemas.") : error);
    mcp_tools_.clear();
    mcp_tool_schema_fingerprint_.clear();
    mcp_tool_schema_changed_ = false;
    mcp_added_tool_names_.clear();
    mcp_removed_tool_names_.clear();
    report.insert(QStringLiteral("ok"), false);
    report.insert(
        QStringLiteral("errors"),
        QStringList{error.isEmpty() ? QStringLiteral("Unable to refresh MCP tool schemas.") : error});
    report.insert(QStringLiteral("canceled"), tools_report.value(QStringLiteral("canceled")));
    report.insert(QStringLiteral("available"), false);
    report.insert(QStringLiteral("status"), mcp_status_);
    report.insert(QStringLiteral("toolCount"), 0);
    report.insert(QStringLiteral("toolNames"), QStringList{});
    report.insert(
        QStringLiteral("schemaCache"),
        mcp_schema_cache_report(
            mcp_tool_schema_revision_,
            mcp_tool_schema_fingerprint_,
            was_primed,
            false,
            {},
            previous_tool_names,
            0));
    return report;
  }

  const auto tool_names = tools_report.value(QStringLiteral("toolNames")).toStringList();
  mcp_tools_ = tools_report.value(QStringLiteral("tools")).toList();
  const auto next_fingerprint = mcp_tool_schema_fingerprint(mcp_tools_);
  const auto next_tool_names = sorted_mcp_tool_names_from_schemas(mcp_tools_);
  mcp_tool_schema_changed_ = was_primed && previous_fingerprint != next_fingerprint;
  if (!was_primed || mcp_tool_schema_changed_) {
    ++mcp_tool_schema_revision_;
  }
  mcp_tool_schema_fingerprint_ = next_fingerprint;
  mcp_added_tool_names_ = string_list_difference(next_tool_names, previous_tool_names);
  mcp_removed_tool_names_ = string_list_difference(previous_tool_names, next_tool_names);
  const auto live_status = client.callTool(QStringLiteral("photo.live_status"));
  if (live_status.value(QStringLiteral("ok")).toBool()) {
    const auto telemetry = live_status.value(QStringLiteral("structuredContent")).toMap();
    setMcpTelemetryState(
        telemetry.value(QStringLiteral("mcpConnectedClientCount")).toInt(),
        telemetry.value(QStringLiteral("mcpTotalClientCount")).toInt(),
        telemetry.value(QStringLiteral("mcpLastToolCallName")).toString(),
        telemetry.value(QStringLiteral("mcpLastToolCallStatus")).toString(),
        telemetry.value(QStringLiteral("mcpLastToolCallAt")).toString());
    report.insert(QStringLiteral("telemetry"), telemetry);
  }
  setMcpStatusState(
      tool_names.isEmpty() ? QStringLiteral("No tools") : QStringLiteral("Ready"),
      tool_names,
      !tool_names.isEmpty(),
      QString());
  report = tools_report;
  report.insert(QStringLiteral("mode"), QStringLiteral("mcp-status"));
  report.insert(QStringLiteral("endpointKind"), mcp_endpoint_kind_);
  report.insert(QStringLiteral("available"), mcp_available_);
  report.insert(QStringLiteral("status"), mcp_status_);
  report.insert(QStringLiteral("toolCount"), mcp_tool_names_.size());
  report.insert(QStringLiteral("toolNames"), mcp_tool_names_);
  report.insert(QStringLiteral("connectedClientCount"), mcp_connected_client_count_);
  report.insert(QStringLiteral("totalClientCount"), mcp_total_client_count_);
  report.insert(QStringLiteral("lastToolCallName"), mcp_last_tool_call_name_);
  report.insert(QStringLiteral("lastToolCallStatus"), mcp_last_tool_call_status_);
  report.insert(QStringLiteral("lastToolCallAt"), mcp_last_tool_call_at_);
  report.insert(
      QStringLiteral("schemaCache"),
      mcp_schema_cache_report(
          mcp_tool_schema_revision_,
          mcp_tool_schema_fingerprint_,
          was_primed,
          mcp_tool_schema_changed_,
          mcp_added_tool_names_,
          mcp_removed_tool_names_,
          static_cast<int>(mcp_tools_.size())));
  return report;
}

QVariantMap AgentEditController::ensureMcpReady(const QString& reason, int max_attempts) {
  const auto bounded_attempts = mcp_endpoint_.trimmed().isEmpty()
                                    ? 1
                                    : std::clamp(max_attempts, 1, 4);
  QVariantList attempts;
  QVariantMap final_report;
  for (auto attempt = 1; attempt <= bounded_attempts; ++attempt) {
    auto attempt_report = refreshMcpTools();
    attempt_report.insert(QStringLiteral("attempt"), attempt);
    attempt_report.insert(QStringLiteral("maxAttempts"), bounded_attempts);
    attempts.append(attempt_report);
    final_report = attempt_report;
    const auto ready = attempt_report.value(QStringLiteral("ok")).toBool() &&
                       attempt_report.value(QStringLiteral("available")).toBool() &&
                       attempt_report.value(QStringLiteral("toolCount")).toInt() > 0;
    if (ready || attempt >= bounded_attempts) {
      break;
    }
    if (QCoreApplication::instance() != nullptr) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(75));
  }

  const auto ready = final_report.value(QStringLiteral("ok")).toBool() &&
                     final_report.value(QStringLiteral("available")).toBool() &&
                     final_report.value(QStringLiteral("toolCount")).toInt() > 0;
  if (!ready && final_report.value(QStringLiteral("errors")).toStringList().isEmpty()) {
    final_report.insert(
        QStringLiteral("errors"),
        QStringList{QStringLiteral("MCP endpoint did not expose usable tools.")});
  }
  final_report.insert(QStringLiteral("ok"), ready);
  final_report.insert(QStringLiteral("mode"), QStringLiteral("mcp-readiness"));
  final_report.insert(QStringLiteral("readinessReason"), reason);
  final_report.insert(QStringLiteral("ready"), ready);
  final_report.insert(QStringLiteral("attemptCount"), attempts.size());
  final_report.insert(QStringLiteral("attempts"), attempts);
  final_report.insert(QStringLiteral("autoRecovered"), ready && attempts.size() > 1);
  final_report.insert(QStringLiteral("recoveryPolicy"), QStringLiteral("retry-discovery-before-agent-planning"));
  final_report.insert(QStringLiteral("requiredForAgentRun"), true);
  return final_report;
}

QVariantMap AgentEditController::prepareSelectedModelInstall() const {
  const auto model = model_metadata_by_id(available_models_, selected_model_id_);
  if (model.isEmpty()) {
    return result_map(false, {QStringLiteral("No valid local model is selected.")});
  }
  QVariantMap report = result_map(true);
  report.insert(QStringLiteral("mode"), QStringLiteral("model-install"));
  report.insert(QStringLiteral("modelId"), selected_model_id_);
  report.insert(QStringLiteral("backend"), model.value(QStringLiteral("backend")));
  report.insert(QStringLiteral("status"), model.value(QStringLiteral("status")));
  report.insert(QStringLiteral("available"), model.value(QStringLiteral("available")));
  report.insert(QStringLiteral("configured"), model.value(QStringLiteral("configured")));
  if (model.value(QStringLiteral("backend")).toString() != QStringLiteral("ollama")) {
    report.insert(QStringLiteral("installable"), false);
    report.insert(QStringLiteral("reason"), QStringLiteral("The selected model is not an Ollama model."));
    return report;
  }
  const auto model_name = model.value(QStringLiteral("model")).toString().trimmed();
  const auto endpoint = model.value(QStringLiteral("endpoint")).toString().trimmed();
  report.insert(QStringLiteral("installable"), !model_name.isEmpty());
  report.insert(QStringLiteral("endpoint"), endpoint);
  report.insert(QStringLiteral("ollamaModel"), model_name);
  report.insert(QStringLiteral("program"), QStringLiteral("ollama"));
  report.insert(QStringLiteral("arguments"), QStringList{QStringLiteral("pull"), model_name});
  report.insert(QStringLiteral("command"), QStringLiteral("ollama pull %1").arg(model_name));
  report.insert(QStringLiteral("confirmationRequired"), true);
  if (model_name.isEmpty()) {
    report.insert(QStringLiteral("ok"), false);
    report.insert(QStringLiteral("errors"), QStringList{QStringLiteral("Ollama model name is missing.")});
  }
  return report;
}

QVariantMap AgentEditController::installSelectedModel(bool confirmed) {
  auto report = prepareSelectedModelInstall();
  report.insert(QStringLiteral("confirmed"), confirmed);
  if (!report.value(QStringLiteral("ok")).toBool()) {
    return report;
  }
  if (!report.value(QStringLiteral("installable")).toBool()) {
    report.insert(QStringLiteral("ok"), false);
    report.insert(QStringLiteral("errors"), QStringList{report.value(QStringLiteral("reason")).toString()});
    return report;
  }
  if (!confirmed) {
    report.insert(QStringLiteral("ok"), false);
    report.insert(QStringLiteral("confirmationRequired"), true);
    report.insert(QStringLiteral("errors"), QStringList{QStringLiteral("Confirm before pulling the Ollama model.")});
    return report;
  }
  if (model_install_process_ != nullptr) {
    report.insert(QStringLiteral("ok"), false);
    report.insert(QStringLiteral("errors"), QStringList{QStringLiteral("A model install is already running.")});
    report.insert(QStringLiteral("running"), true);
    return report;
  }

  auto program = qEnvironmentVariable("PHOTO_EDITOR_OLLAMA_BINARY").trimmed();
  if (program.isEmpty()) {
    program = QStandardPaths::findExecutable(QStringLiteral("ollama"));
  }
  if (program.isEmpty()) {
    report.insert(QStringLiteral("ok"), false);
    report.insert(QStringLiteral("errors"), QStringList{QStringLiteral("Ollama executable was not found in PATH.")});
    return report;
  }

  const auto arguments = report.value(QStringLiteral("arguments")).toStringList();
  auto* process = new QProcess(this);
  process->setProgram(program);
  process->setArguments(arguments);
  process->setProcessChannelMode(QProcess::MergedChannels);
  model_install_process_ = process;
  setModelInstallState(
      true,
      QStringLiteral("Pulling %1").arg(report.value(QStringLiteral("ollamaModel")).toString()),
      QString(),
      QVariantMap{
          {QStringLiteral("status"), QStringLiteral("starting")},
          {QStringLiteral("ollamaModel"), report.value(QStringLiteral("ollamaModel"))},
          {QStringLiteral("progress"), 0.0},
          {QStringLiteral("progressPercent"), 0},
      });

  connect(process, &QProcess::readyReadStandardOutput, this, [this, process]() {
    const auto chunk = QString::fromUtf8(process->readAllStandardOutput());
    const auto output = (model_install_output_ + chunk).trimmed();
    auto progress = parse_ollama_pull_progress(output);
    if (!progress.contains(QStringLiteral("progress")) &&
        model_install_progress_.contains(QStringLiteral("progress"))) {
      progress.insert(QStringLiteral("progress"), model_install_progress_.value(QStringLiteral("progress")));
      progress.insert(QStringLiteral("progressPercent"), model_install_progress_.value(QStringLiteral("progressPercent")));
    }
    setModelInstallState(
        true,
        model_install_status_from_progress(progress, model_install_status_),
        output,
        progress);
  });
  connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError) {
    const auto error = process->errorString();
    model_install_process_ = nullptr;
    setModelInstallState(false, QStringLiteral("Install failed"), error);
    process->deleteLater();
  });
  connect(process, &QProcess::finished, this, [this, process](int exit_code, QProcess::ExitStatus exit_status) {
    const auto output =
        (model_install_output_ + QString::fromUtf8(process->readAllStandardOutput())).trimmed();
    const auto ok = exit_status == QProcess::NormalExit && exit_code == 0;
    auto progress = parse_ollama_pull_progress(output);
    if (ok) {
      progress.insert(QStringLiteral("status"), QStringLiteral("success"));
      progress.insert(QStringLiteral("progress"), 1.0);
      progress.insert(QStringLiteral("progressPercent"), 100);
    }
    model_install_process_ = nullptr;
    setModelInstallState(
        false,
        ok ? QStringLiteral("Installed") : QStringLiteral("Install failed"),
        output,
        progress);
    reloadModels();
    process->deleteLater();
  });

  process->start();
  if (!process->waitForStarted(5000)) {
    const auto error = process->errorString();
    model_install_process_ = nullptr;
    process->deleteLater();
    setModelInstallState(false, QStringLiteral("Install failed"), error);
    report.insert(QStringLiteral("ok"), false);
    report.insert(QStringLiteral("errors"), QStringList{error});
    return report;
  }

  report.insert(QStringLiteral("started"), true);
  report.insert(QStringLiteral("running"), true);
  report.insert(QStringLiteral("programPath"), program);
  return report;
}

QVariantMap AgentEditController::probeSelectedModel() {
  auto report = probe_agent_local_model(selected_model_id_);
  report.insert(QStringLiteral("probedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
  const auto model_id = report.value(QStringLiteral("modelId")).toString();
  auto changed = false;
  for (auto& model_value : available_models_) {
    auto model = model_value.toMap();
    if (model.value(QStringLiteral("id")).toString() != model_id) {
      continue;
    }
    model.insert(QStringLiteral("available"), report.value(QStringLiteral("available")));
    model.insert(QStringLiteral("status"), report.value(QStringLiteral("status")));
    model.insert(QStringLiteral("lastProbeAtUtc"), report.value(QStringLiteral("probedAtUtc")));
    if (report.contains(QStringLiteral("statusReason"))) {
      model.insert(QStringLiteral("statusReason"), report.value(QStringLiteral("statusReason")));
    } else {
      model.remove(QStringLiteral("statusReason"));
    }
    if (report.contains(QStringLiteral("modelFamily"))) {
      model.insert(QStringLiteral("modelFamily"), report.value(QStringLiteral("modelFamily")));
    }
    if (report.contains(QStringLiteral("parameterSize"))) {
      model.insert(QStringLiteral("parameterSize"), report.value(QStringLiteral("parameterSize")));
    }
    if (report.contains(QStringLiteral("quantizationLevel"))) {
      model.insert(QStringLiteral("quantizationLevel"), report.value(QStringLiteral("quantizationLevel")));
    }
    model_value = model;
    changed = true;
    break;
  }
  if (changed) {
    emit availableModelsChanged();
  }
  setModelInstallState(
      false,
      report.value(QStringLiteral("ok")).toBool() ? QStringLiteral("Model available") : QStringLiteral("Model unavailable"),
      report.value(QStringLiteral("statusReason")).toString());
  return report;
}

QVariantMap AgentEditController::setupLocalAi(bool confirmed) {
  QElapsedTimer timer;
  timer.start();
  QVariantList checks;
  QStringList errors;
  QStringList warnings;
  const auto started_at_utc = QDateTime::currentDateTimeUtc();
  const auto model = model_metadata_by_id(available_models_, selected_model_id_);
  const auto model_backend = model.value(QStringLiteral("backend")).toString();
  const auto model_name = model.value(QStringLiteral("model")).toString();
  const auto add_check = [&](const QString& key,
                             const QString& label,
                             bool ok,
                             const QString& status,
                             const QVariantMap& details = {},
                             bool required = true) {
    QVariantMap check{
        {QStringLiteral("key"), key},
        {QStringLiteral("label"), label},
        {QStringLiteral("ok"), ok},
        {QStringLiteral("status"), status},
        {QStringLiteral("required"), required},
    };
    if (!details.isEmpty()) {
      check.insert(QStringLiteral("details"), details);
    }
    checks.append(check);
    if (required && !ok) {
      errors.append(status);
    }
  };

  auto report = result_map(true);
  report.insert(QStringLiteral("mode"), QStringLiteral("local-ai-setup"));
  report.insert(QStringLiteral("confirmed"), confirmed);
  report.insert(QStringLiteral("startedAtUtc"), started_at_utc.toString(Qt::ISODateWithMs));
  report.insert(QStringLiteral("modelId"), selected_model_id_);
  report.insert(QStringLiteral("backend"), model_backend);
  report.insert(QStringLiteral("setupMemoryPacket"), memoryPacketForPhase(QStringLiteral("setup")));

  const auto install_plan = prepareSelectedModelInstall();
  report.insert(QStringLiteral("installPlan"), install_plan);
  const auto ollama_program =
      qEnvironmentVariable("PHOTO_EDITOR_OLLAMA_BINARY").trimmed().isEmpty()
          ? QStandardPaths::findExecutable(QStringLiteral("ollama"))
          : qEnvironmentVariable("PHOTO_EDITOR_OLLAMA_BINARY").trimmed();
  const auto ollama_required = model_backend == QStringLiteral("ollama");
  add_check(
      QStringLiteral("ollama_installed"),
      QStringLiteral("Ollama installed"),
      !ollama_required || !ollama_program.trimmed().isEmpty(),
      !ollama_required
          ? QStringLiteral("Not required for the selected local model.")
          : !ollama_program.trimmed().isEmpty()
                ? QStringLiteral("Ollama executable found.")
                : QStringLiteral("Ollama executable was not found."),
      QVariantMap{{QStringLiteral("programPath"), ollama_program}},
      ollama_required);

  const auto probe = probeSelectedModel();
  report.insert(QStringLiteral("modelProbe"), probe);
  const auto model_ready = probe.value(QStringLiteral("ok")).toBool() &&
                           probe.value(QStringLiteral("available")).toBool();
  add_check(
      QStringLiteral("ollama_reachable"),
      QStringLiteral("Ollama reachable"),
      !ollama_required || model_ready,
      !ollama_required
          ? QStringLiteral("Not required for the selected local model.")
          : model_ready
                ? QStringLiteral("Ollama responded to the local probe.")
                : probe.value(QStringLiteral("statusReason"),
                              QStringLiteral("Ollama did not respond to the local probe.")).toString(),
      QVariantMap{{QStringLiteral("endpoint"), probe.value(QStringLiteral("endpoint"))}},
      ollama_required);
  add_check(
      QStringLiteral("selected_model_installed"),
      QStringLiteral("Selected local model installed"),
      model_ready,
      model_ready
          ? QStringLiteral("Selected model is available.")
          : probe.value(QStringLiteral("statusReason"),
                        QStringLiteral("Selected model is not available.")).toString(),
      QVariantMap{{QStringLiteral("ollamaModel"), model_name}},
      true);
  add_check(
      QStringLiteral("model_can_respond"),
      QStringLiteral("Model can respond"),
      probe.value(QStringLiteral("ok")).toBool(),
      probe.value(QStringLiteral("ok")).toBool()
          ? QStringLiteral("Model probe completed.")
          : probe.value(QStringLiteral("statusReason"),
                        QStringLiteral("Model probe failed.")).toString(),
      {},
      true);

  QVariantMap install_report;
  if (!model_ready && confirmed && install_plan.value(QStringLiteral("installable")).toBool()) {
    install_report = installSelectedModel(true);
    report.insert(QStringLiteral("installReport"), install_report);
    if (!install_report.value(QStringLiteral("ok")).toBool()) {
      warnings.append(install_report.value(QStringLiteral("errors")).toStringList());
    }
  } else if (!model_ready && install_plan.value(QStringLiteral("installable")).toBool()) {
    report.insert(QStringLiteral("nextAction"), QStringLiteral("confirm_model_download"));
  }

  QVariantMap mcp_report;
  if (mcp_endpoint_.trimmed().isEmpty()) {
    mcp_report = QVariantMap{
        {QStringLiteral("ok"), true},
        {QStringLiteral("available"), false},
        {QStringLiteral("status"), QStringLiteral("Not configured")},
        {QStringLiteral("skipped"), true},
    };
    add_check(
        QStringLiteral("mcp_tools_reachable"),
        QStringLiteral("MCP tools reachable"),
        true,
        QStringLiteral("MCP endpoint is not configured yet."),
        mcp_report,
        false);
  } else {
    mcp_report = ensureMcpReady(QStringLiteral("local-ai-setup"), 3);
    add_check(
        QStringLiteral("mcp_tools_reachable"),
        QStringLiteral("MCP tools reachable"),
        mcp_report.value(QStringLiteral("ready")).toBool(),
        mcp_report.value(QStringLiteral("ready")).toBool()
            ? QStringLiteral("MCP tools are reachable.")
            : mcp_report.value(QStringLiteral("failureReason"),
                               QStringLiteral("MCP tools are not reachable.")).toString(),
        mcp_report,
        true);
  }
  report.insert(QStringLiteral("mcpReadiness"), mcp_report);

  QVariantMap proof_report;
  const auto style = selectedStyle();
  const auto assets = scoped_asset_maps(catalog_model_, inferred_agent_scope(catalog_model_));
  if (catalog_model_ == nullptr || style.isEmpty() || assets.isEmpty()) {
    proof_report = QVariantMap{
        {QStringLiteral("ok"), true},
        {QStringLiteral("skipped"), true},
        {QStringLiteral("status"), QStringLiteral("Proof test waits for a selected photo and style.")},
    };
    add_check(
        QStringLiteral("proof_rendering"),
        QStringLiteral("App can render before/after proofs"),
        true,
        proof_report.value(QStringLiteral("status")).toString(),
        proof_report,
        false);
  } else {
    const auto asset = assets.first().toMap();
    const auto recipe_json = asset.value(QStringLiteral("recipeJson")).toString();
    const auto recipe_validation = validate_edit_recipe_json(recipe_json);
    if (recipe_validation.valid) {
      const auto state = parse_edit_recipe_json(recipe_json);
      proof_report = render_agent_proof_artifacts(
          asset.value(QStringLiteral("filePath")).toString(),
          state,
          state,
          style,
          QUuid::createUuid().toString(QUuid::WithoutBraces),
          asset.value(QStringLiteral("assetId")).toString(),
          {},
          []() { return false; });
    } else {
      proof_report = result_map(false, recipe_validation.errors);
    }
    add_check(
        QStringLiteral("proof_rendering"),
        QStringLiteral("App can render before/after proofs"),
        proof_report.value(QStringLiteral("ok")).toBool(),
        proof_report.value(QStringLiteral("ok")).toBool()
            ? QStringLiteral("Before/after proof rendered.")
            : proof_report.value(QStringLiteral("failureReason"),
                                 QStringLiteral("Before/after proof rendering failed.")).toString(),
        proof_report,
        true);
  }
  report.insert(QStringLiteral("proofReadiness"), proof_report);
  const auto model_can_inspect_proof =
      proof_report.value(QStringLiteral("ok")).toBool() &&
      (model_backend != QStringLiteral("ollama") || model.value(QStringLiteral("vision")).toBool());
  add_check(
      QStringLiteral("model_can_inspect_proof_image"),
      QStringLiteral("Model can inspect a proof image"),
      model_can_inspect_proof,
      model_can_inspect_proof
          ? QStringLiteral("Proof image is available to the selected model.")
          : QStringLiteral("The selected model cannot use the rendered proof image."),
      QVariantMap{
          {QStringLiteral("vision"), model.value(QStringLiteral("vision"))},
          {QStringLiteral("proofSkipped"), proof_report.value(QStringLiteral("skipped")).toBool()},
      },
      !proof_report.value(QStringLiteral("skipped")).toBool());

  auto setup_complete = true;
  for (const auto& check_value : checks) {
    const auto check = check_value.toMap();
    if (check.value(QStringLiteral("required")).toBool() &&
        !check.value(QStringLiteral("ok")).toBool()) {
      setup_complete = false;
      break;
    }
  }
  report.insert(QStringLiteral("checks"), checks);
  report.insert(QStringLiteral("checkCount"), checks.size());
  report.insert(QStringLiteral("setupComplete"), setup_complete);
  report.insert(QStringLiteral("setupRequired"), !setup_complete);
  report.insert(QStringLiteral("primaryAction"), setup_complete ? QStringLiteral("ready")
                                                                : QStringLiteral("set_up_local_ai"));
  report.insert(QStringLiteral("durationMs"), timer.elapsed());
  if (!setup_complete) {
    report.insert(QStringLiteral("ok"), false);
    report.insert(QStringLiteral("errors"), errors);
    record_failure_reason(&report, errors.join(QStringLiteral("; ")));
  }
  if (!warnings.isEmpty()) {
    report.insert(QStringLiteral("warnings"), warnings);
  }
  setLastRunReport(report);
  setStatus(setup_complete ? QStringLiteral("Local AI ready") : QStringLiteral("Local AI setup blocked"));
  setErrorMessage(setup_complete ? QString() : report.value(QStringLiteral("failureReason")).toString());
  return report;
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

QVariantMap AgentEditController::mcpToolPolicyForPhase(const QString& phase) const {
  const auto normalized_phase = normalized_agent_phase(phase);
  const auto tool_names = mcp_tools_for_agent_phase(normalized_phase);
  QVariantMap report{
      {QStringLiteral("ok"), true},
      {QStringLiteral("mode"), QStringLiteral("mcp-tool-policy")},
      {QStringLiteral("phase"), normalized_phase},
      {QStringLiteral("toolNames"), tool_names},
      {QStringLiteral("toolCount"), tool_names.size()},
      {QStringLiteral("leastPrivilege"), true},
  };
  const QVariantList filtered_tools = filter_mcp_tools_for_phase(mcp_tools_, normalized_phase);
  report.insert(QStringLiteral("availableToolSchemas"), filtered_tools);
  report.insert(QStringLiteral("availableToolCount"), filtered_tools.size());
  report.insert(
      QStringLiteral("toolBudget"),
      mcp_tool_budget_report(normalized_phase, tool_names, static_cast<int>(filtered_tools.size())));
  report.insert(
      QStringLiteral("schemaCache"),
      mcp_schema_cache_report(
          mcp_tool_schema_revision_,
          mcp_tool_schema_fingerprint_,
          mcp_tool_schema_revision_ > 0,
          mcp_tool_schema_changed_,
          mcp_added_tool_names_,
          mcp_removed_tool_names_,
          static_cast<int>(mcp_tools_.size())));
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

QVariantMap AgentEditController::createLearnedStyleProfileFromSelection(
    const QString& name,
    const QString& goal,
    const QString& notes) {
  const auto trimmed_name = name.trimmed().isEmpty()
                                ? QStringLiteral("Learned Project Style")
                                : name.trimmed();
  const auto trimmed_goal = goal.trimmed().isEmpty()
                                ? QStringLiteral("Edit selected photos consistently in the user's demonstrated style.")
                                : goal.trimmed();
  if (catalog_model_ == nullptr) {
    return result_map(false, {QStringLiteral("Catalog model is not connected.")});
  }
  const auto assets = scoped_asset_maps(catalog_model_, QStringLiteral("selected-assets"));
  if (assets.isEmpty()) {
    return result_map(false, {QStringLiteral("Select one or more edited photos before learning a style profile.")});
  }

  const auto base_id = normalized_style_id(trimmed_name);
  auto style_id = base_id;
  auto suffix = 2;
  const QDir directory(styleDirectory());
  while (QFileInfo::exists(directory.filePath(style_id + QStringLiteral(".json")))) {
    style_id = QStringLiteral("%1-%2").arg(base_id).arg(suffix);
    ++suffix;
  }

  const QVariantMap allowed_tools{
      {QStringLiteral("raw"), true},
      {QStringLiteral("global_color"), true},
      {QStringLiteral("curves"), true},
      {QStringLiteral("hsl"), true},
      {QStringLiteral("color_grading"), true},
      {QStringLiteral("detail"), true},
      {QStringLiteral("crop"), QStringLiteral("conservative")},
      {QStringLiteral("masks"), true},
      {QStringLiteral("retouch"), QStringLiteral("minor_cleanup_only")},
      {QStringLiteral("layers_luts"), false},
      {QStringLiteral("export"), false},
  };
  const QVariantMap constraints{
      {QStringLiteral("avoid_clipped_highlights"), true},
      {QStringLiteral("avoid_crushed_blacks"), true},
      {QStringLiteral("max_exposure_shift"), 1.2},
      {QStringLiteral("max_temperature_shift"), 1800},
      {QStringLiteral("max_saturation_shift"), 18},
      {QStringLiteral("protect_skin_texture"), true},
      {QStringLiteral("color_constraints"),
       QVariantMap{
           {QStringLiteral("preferred_temperature_bias"), QStringLiteral("neutral")},
           {QStringLiteral("protected_hues"), QStringList{QStringLiteral("skin"), QStringLiteral("neutral")}},
           {QStringLiteral("max_hsl_channel_shift"), 18.0},
           {QStringLiteral("max_color_grading_saturation"), 16.0},
           {QStringLiteral("allow_harmony"), true},
       }},
  };
  const QVariantMap validation{
      {QStringLiteral("highlight_clip_limit_percent"), 0.5},
      {QStringLiteral("shadow_clip_limit_percent"), 1.0},
      {QStringLiteral("require_before_after_proof"), true},
      {QStringLiteral("require_skin_tone_check"), true},
      {QStringLiteral("require_style_reference_check"), true},
  };
  const QVariantMap batch_behavior{
      {QStringLiteral("cluster_by_lighting"), true},
      {QStringLiteral("maintain_sequence_consistency"), true},
      {QStringLiteral("correct_outliers_individually"), true},
      {QStringLiteral("sample_first"), true},
  };
  const QVariantMap reference_policy{
      {QStringLiteral("mode"), QStringLiteral("learned_style_profile")},
      {QStringLiteral("reference_image_matching"), QStringLiteral("metadata_only")},
      {QStringLiteral("examples_supported"), true},
      {QStringLiteral("before_after_pairs_supported"), true},
      {QStringLiteral("embedded_image_payloads"), false},
  };
  const auto learned_profile = learned_style_profile_from_assets(assets, notes, trimmed_goal);
  const QVariantMap style{
      {QStringLiteral("schema_version"), kCurrentAgentEditStyleSchemaVersion},
      {QStringLiteral("id"), style_id},
      {QStringLiteral("name"), trimmed_name},
      {QStringLiteral("goal"), trimmed_goal},
      {QStringLiteral("allowed_tools"), allowed_tools},
      {QStringLiteral("constraints"), constraints},
      {QStringLiteral("validation"), validation},
      {QStringLiteral("batch_behavior"), batch_behavior},
      {QStringLiteral("reference_policy"), reference_policy},
      {QStringLiteral("learned_profile"), learned_profile},
      {QStringLiteral("tool_knowledge"), default_agent_tool_knowledge()},
      {QStringLiteral("quality_model"), default_agent_quality_model()},
      {QStringLiteral("project_workflow"), default_agent_project_workflow()},
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
  report.insert(QStringLiteral("mode"), QStringLiteral("learned-style-profile"));
  report.insert(QStringLiteral("styleId"), style_id);
  report.insert(QStringLiteral("styleName"), trimmed_name);
  report.insert(QStringLiteral("sourceAssetCount"), assets.size());
  report.insert(QStringLiteral("usableRecipeCount"), learned_profile.value(QStringLiteral("usable_recipe_count")));
  report.insert(QStringLiteral("skippedRecipeCount"), learned_profile.value(QStringLiteral("skipped_recipe_count")));
  report.insert(QStringLiteral("style"), style);
  report.insert(QStringLiteral("stylePath"), directory.filePath(style_id + QStringLiteral(".json")));
  const auto memory_report = write_agent_style_memory_note(AppBootstrap::appDataLocation(), style);
  const auto tool_memory_report = write_agent_tool_memory_note(AppBootstrap::appDataLocation(), style);
  report.insert(QStringLiteral("memory"), memory_report);
  report.insert(QStringLiteral("toolMemory"), tool_memory_report);
  report.insert(QStringLiteral("memoryNotePath"), memory_report.value(QStringLiteral("path")));
  report.insert(QStringLiteral("toolMemoryNotePath"), tool_memory_report.value(QStringLiteral("path")));
  report.insert(
      QStringLiteral("userVisibleMemoryEvents"),
      QVariantList{user_visible_memory_event(
          QStringLiteral("style_profile_created"),
          style_id,
          trimmed_name,
          QStringLiteral("Learned style memory created"),
          QStringLiteral("Created durable style memory from %1 selected photo%2.")
              .arg(assets.size())
              .arg(assets.size() == 1 ? QString() : QStringLiteral("s")),
          QVariantMap{
              {QStringLiteral("styleMemoryNotePath"), memory_report.value(QStringLiteral("path"))},
              {QStringLiteral("toolMemoryNotePath"), tool_memory_report.value(QStringLiteral("path"))},
          })});
  if (!memory_report.value(QStringLiteral("ok")).toBool() ||
      !tool_memory_report.value(QStringLiteral("ok")).toBool()) {
    auto warnings = report.value(QStringLiteral("warnings")).toStringList();
    warnings.append(memory_report.value(QStringLiteral("errors")).toStringList());
    warnings.append(tool_memory_report.value(QStringLiteral("errors")).toStringList());
    report.insert(QStringLiteral("warnings"), warnings);
  }
  setLastRunReport(report);
  observability::logInfo(
      photoEditorAppLog(),
      QStringLiteral("agent.edit.learned_style_created"),
      {{QStringLiteral("style_id"), style_id},
       {QStringLiteral("source_asset_count"), assets.size()}});
  return report;
}

QVariantMap AgentEditController::planStyleProjectRun(const QString& scope) {
  QStringList errors;
  if (catalog_model_ == nullptr) {
    errors.append(QStringLiteral("Catalog model is not connected."));
  }
  const auto style = selectedStyle();
  if (style.isEmpty()) {
    errors.append(QStringLiteral("No valid agent style is selected."));
  }
  const auto assets = scoped_asset_maps(catalog_model_, scope);
  const auto report_scope = report_scope_for_assets(scope, assets);
  if (catalog_model_ != nullptr && assets.isEmpty()) {
    errors.append(empty_scope_error(scope, QStringLiteral("project style planning")));
  }
  if (!errors.isEmpty()) {
    auto report = result_map(false, errors);
    report.insert(QStringLiteral("mode"), QStringLiteral("style-project-plan"));
    setLastRunReport(report);
    return report;
  }

  const auto cluster_metadata = batch_cluster_metadata(assets);
  const auto before_after_pairs = inferred_before_after_pairs(assets);
  QVariantList asset_summaries;
  for (const auto& asset_value : assets) {
    const auto asset = asset_value.toMap();
    const auto recipe_json = asset.value(QStringLiteral("recipeJson")).toString();
    QVariantMap summary{
        {QStringLiteral("assetId"), asset.value(QStringLiteral("assetId"))},
        {QStringLiteral("filePath"), asset.value(QStringLiteral("filePath"))},
        {QStringLiteral("captureDate"), asset.value(QStringLiteral("captureDate"))},
        {QStringLiteral("cameraLabel"), asset.value(QStringLiteral("cameraLabel"))},
        {QStringLiteral("lensModel"), asset.value(QStringLiteral("lensModel"))},
        {QStringLiteral("sourceAssetId"), asset.value(QStringLiteral("sourceAssetId"))},
        {QStringLiteral("virtualCopy"), asset.value(QStringLiteral("virtualCopy"))},
        {QStringLiteral("variantName"), asset.value(QStringLiteral("variantName"))},
    };
    const auto recipe_validation = validate_edit_recipe_json(recipe_json);
    summary.insert(QStringLiteral("recipeValid"), recipe_validation.valid);
    if (recipe_validation.valid) {
      summary.insert(QStringLiteral("recipeSummary"), recipe_analysis_summary(parse_edit_recipe_json(recipe_json), recipe_json));
    } else {
      summary.insert(QStringLiteral("recipeErrors"), recipe_validation.errors);
    }
    asset_summaries.append(summary);
  }

  const auto learned_profile = style.value(QStringLiteral("learnedProfile")).toMap();
  QVariantMap report = result_map(true);
  report.insert(QStringLiteral("mode"), QStringLiteral("style-project-plan"));
  report.insert(QStringLiteral("scope"), report_scope);
  report.insert(QStringLiteral("styleId"), selected_style_id_);
  report.insert(QStringLiteral("styleName"), style.value(QStringLiteral("name")));
  report.insert(QStringLiteral("assetCount"), assets.size());
  report.insert(QStringLiteral("assetIds"), asset_ids_from_maps(assets));
  report.insert(QStringLiteral("assetSummaries"), asset_summaries);
  report.insert(QStringLiteral("batchClusters"), cluster_metadata.value(QStringLiteral("batchClusters")));
  report.insert(QStringLiteral("clusterCount"), cluster_metadata.value(QStringLiteral("clusterCount")));
  report.insert(QStringLiteral("representativeAssetIds"), cluster_metadata.value(QStringLiteral("representativeAssetIds")));
  report.insert(QStringLiteral("assetClusterAssignments"), cluster_metadata.value(QStringLiteral("assetClusterAssignments")));
  report.insert(QStringLiteral("beforeAfterPairs"), before_after_pairs);
  report.insert(QStringLiteral("beforeAfterPairCount"), before_after_pairs.size());
  report.insert(QStringLiteral("sampleFirst"), true);
  report.insert(QStringLiteral("sampleAssetIds"), cluster_metadata.value(QStringLiteral("representativeAssetIds")));
  report.insert(QStringLiteral("validationRequired"), true);
  report.insert(QStringLiteral("styleReferenceAssetCount"), learned_profile.value(QStringLiteral("source_asset_count")));
  report.insert(QStringLiteral("styleMatchScoring"), QStringLiteral("metadata_recipe_visual_fingerprint_v1"));
  report.insert(QStringLiteral("planSummary"),
                QStringLiteral("Plan %1 selected/project photo(s) in %2 cluster(s), edit representative samples first, validate every image, and review style-match outliers.")
                    .arg(assets.size())
                    .arg(cluster_metadata.value(QStringLiteral("clusterCount")).toInt()));
  const auto project_memory_report =
      write_agent_project_memory_note(AppBootstrap::appDataLocation(), style, report);
  report.insert(QStringLiteral("projectMemory"), project_memory_report);
  report.insert(QStringLiteral("projectMemoryNotePath"), project_memory_report.value(QStringLiteral("path")));
  if (!project_memory_report.value(QStringLiteral("ok")).toBool()) {
    auto warnings = report.value(QStringLiteral("warnings")).toStringList();
    warnings.append(project_memory_report.value(QStringLiteral("errors")).toStringList());
    report.insert(QStringLiteral("warnings"), warnings);
  }
  setLastRunReport(report);
  return report;
}

QVariantMap AgentEditController::recordStyleFeedback(
    const QString& feedback,
    const QVariantMap& context) {
  const auto trimmed_feedback = feedback.trimmed();
  if (trimmed_feedback.isEmpty()) {
    return result_map(false, {QStringLiteral("Feedback text is required.")});
  }
  auto style = selectedStyle();
  if (style.isEmpty()) {
    return result_map(false, {QStringLiteral("No valid agent style is selected.")});
  }
  const auto source = style.value(QStringLiteral("source")).toString();
  if (source == QStringLiteral("built-in") || source.trimmed().isEmpty()) {
    return result_map(false, {QStringLiteral("Select or create a saved learned style profile before recording feedback.")});
  }

  auto learned_profile = style.value(QStringLiteral("learnedProfile")).toMap();
  if (learned_profile.isEmpty()) {
    learned_profile.insert(QStringLiteral("source"), QStringLiteral("user_feedback"));
  }
  auto feedback_events = learned_profile.value(QStringLiteral("feedback_events")).toList();
  feedback_events.append(QVariantMap{
      {QStringLiteral("createdAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
      {QStringLiteral("feedback"), trimmed_feedback},
      {QStringLiteral("context"), context},
      {QStringLiteral("lastRunId"), last_run_report_.value(QStringLiteral("runId"))},
      {QStringLiteral("lastRunMode"), last_run_report_.value(QStringLiteral("mode"))},
  });
  learned_profile.insert(QStringLiteral("feedback_events"), feedback_events);
  learned_profile.insert(QStringLiteral("feedback_event_count"), feedback_events.size());
  learned_profile.insert(QStringLiteral("latestFeedback"), trimmed_feedback);
  auto consistency_guidelines = learned_profile.value(QStringLiteral("consistencyGuidelines")).toStringList();
  if (!consistency_guidelines.contains(trimmed_feedback)) {
    consistency_guidelines.append(trimmed_feedback);
  }
  learned_profile.insert(QStringLiteral("consistencyGuidelines"), consistency_guidelines);
  style.insert(QStringLiteral("learnedProfile"), learned_profile);
  auto project_workflow = style.value(QStringLiteral("projectWorkflow")).toMap();
  project_workflow.insert(QStringLiteral("feedbackEventCount"), feedback_events.size());
  project_workflow.insert(QStringLiteral("feedbackUpdatesActiveProfile"), true);
  style.insert(QStringLiteral("projectWorkflow"), project_workflow);

  auto document = style_storage_document_from_runtime_style(style);
  QString error_message;
  if (!save_agent_edit_style_to_directory(styleDirectory(), document, &error_message)) {
    setErrorMessage(error_message);
    return result_map(false, {error_message});
  }
  reloadStyles();
  selected_style_id_ = document.value(QStringLiteral("id")).toString();
  emit selectedStyleChanged();

  QVariantMap report = result_map(true);
  report.insert(QStringLiteral("mode"), QStringLiteral("style-feedback"));
  report.insert(QStringLiteral("styleId"), selected_style_id_);
  report.insert(QStringLiteral("feedbackEventCount"), feedback_events.size());
  report.insert(QStringLiteral("feedback"), trimmed_feedback);
  report.insert(QStringLiteral("stylePath"), QDir(styleDirectory()).filePath(selected_style_id_ + QStringLiteral(".json")));
  const auto style_memory_report =
      write_agent_style_memory_note(AppBootstrap::appDataLocation(), document);
  const auto tool_memory_report =
      write_agent_tool_memory_note(AppBootstrap::appDataLocation(), document);
  const auto feedback_memory_report = write_agent_feedback_memory_note(
      AppBootstrap::appDataLocation(),
      document,
      trimmed_feedback,
      context,
      last_run_report_);
  const auto style_change_summary = style_change_summary_from_feedback(trimmed_feedback);
  report.insert(QStringLiteral("styleMemory"), style_memory_report);
  report.insert(QStringLiteral("toolMemory"), tool_memory_report);
  report.insert(QStringLiteral("feedbackMemory"), feedback_memory_report);
  report.insert(QStringLiteral("memoryNotePath"), style_memory_report.value(QStringLiteral("path")));
  report.insert(QStringLiteral("toolMemoryNotePath"), tool_memory_report.value(QStringLiteral("path")));
  report.insert(QStringLiteral("feedbackMemoryNotePath"), feedback_memory_report.value(QStringLiteral("path")));
  report.insert(QStringLiteral("styleChangeSummary"), style_change_summary);
  report.insert(QStringLiteral("styleChangeReason"), trimmed_feedback);
  auto memory_event = user_visible_memory_event(
      QStringLiteral("style_feedback_saved"),
      selected_style_id_,
      document.value(QStringLiteral("name")).toString(),
      QStringLiteral("Style memory updated"),
      style_change_summary,
      QVariantMap{
          {QStringLiteral("styleMemoryNotePath"), style_memory_report.value(QStringLiteral("path"))},
          {QStringLiteral("toolMemoryNotePath"), tool_memory_report.value(QStringLiteral("path"))},
          {QStringLiteral("feedbackMemoryNotePath"), feedback_memory_report.value(QStringLiteral("path"))},
      });
  memory_event.insert(QStringLiteral("summary"), style_change_summary);
  memory_event.insert(QStringLiteral("styleChangeSummary"), style_change_summary);
  memory_event.insert(QStringLiteral("styleChangeReason"), trimmed_feedback);
  memory_event.insert(QStringLiteral("memoryWriteApproved"), true);
  report.insert(
      QStringLiteral("userVisibleMemoryEvents"),
      QVariantList{memory_event});
  if (!style_memory_report.value(QStringLiteral("ok")).toBool() ||
      !tool_memory_report.value(QStringLiteral("ok")).toBool() ||
      !feedback_memory_report.value(QStringLiteral("ok")).toBool()) {
    auto warnings = report.value(QStringLiteral("warnings")).toStringList();
    warnings.append(style_memory_report.value(QStringLiteral("errors")).toStringList());
    warnings.append(tool_memory_report.value(QStringLiteral("errors")).toStringList());
    warnings.append(feedback_memory_report.value(QStringLiteral("errors")).toStringList());
    report.insert(QStringLiteral("warnings"), warnings);
  }
  setLastRunReport(report);
  return report;
}

QVariantMap AgentEditController::memoryVaultStatus() const {
  auto report = agent_memory_vault_status(AppBootstrap::appDataLocation());
  const auto current_snapshot = agent_memory_vault_snapshot(AppBootstrap::appDataLocation());
  if (!memory_vault_snapshot_initialized_) {
    memory_vault_snapshot_ = current_snapshot;
    memory_vault_snapshot_initialized_ = true;
  }
  const auto current_fingerprint = current_snapshot.value(QStringLiteral("fingerprint")).toString();
  const auto known_fingerprint =
      memory_vault_snapshot_.value(QStringLiteral("fingerprint")).toString();
  const auto external_change_detected = current_fingerprint != known_fingerprint;
  const auto changed_files = external_change_detected
                                 ? memory_snapshot_changes(memory_vault_snapshot_, current_snapshot)
                                 : QVariantList{};
  const auto style = selectedStyle();
  report.insert(QStringLiteral("selectedStyleId"), selected_style_id_);
  report.insert(QStringLiteral("selectedStyleName"), style.value(QStringLiteral("name")));
  report.insert(
      QStringLiteral("activeStyleNotePath"),
      selected_style_id_.trimmed().isEmpty()
          ? QString()
          : agent_memory_style_note_path(AppBootstrap::appDataLocation(), selected_style_id_));
  report.insert(QStringLiteral("activePhasePacket"), memoryPacketForPhase(QStringLiteral("sample")));
  report.insert(QStringLiteral("vaultFingerprint"), current_fingerprint);
  report.insert(QStringLiteral("lastKnownVaultFingerprint"), known_fingerprint);
  report.insert(QStringLiteral("externalChangeDetected"), external_change_detected);
  report.insert(QStringLiteral("externalChangeCount"), changed_files.size());
  report.insert(QStringLiteral("externalChangedFiles"), changed_files);
  report.insert(
      QStringLiteral("externalChangePolicy"),
      QStringLiteral("warn-user-before-overwriting-obsidian-edits"));
  return report;
}

QVariantMap AgentEditController::exportMemoryVault(
    const QString& destination_directory,
    bool redacted) const {
  return export_agent_memory_vault(
      AppBootstrap::appDataLocation(),
      destination_directory,
      redacted);
}

QVariantMap AgentEditController::openMemoryVault() const {
  const auto root_path = agent_memory_root_directory(AppBootstrap::appDataLocation());
  const auto errors = ensure_agent_memory_vault_sections(root_path);

  QVariantMap report{
      {QStringLiteral("ok"), errors.isEmpty()},
      {QStringLiteral("mode"), QStringLiteral("agent-memory-vault-open")},
      {QStringLiteral("rootPath"), root_path},
      {QStringLiteral("errors"), errors},
  };
  if (!errors.isEmpty()) {
    report.insert(QStringLiteral("failureReason"), errors.join(QStringLiteral("; ")));
    return report;
  }

  const auto opened = QDesktopServices::openUrl(QUrl::fromLocalFile(root_path));
  report.insert(QStringLiteral("opened"), opened);
  if (!opened) {
    report.insert(
        QStringLiteral("warnings"),
        QStringList{QStringLiteral("Unable to open agent memory vault in the system file browser.")});
  }
  refreshMemoryVaultSnapshot();
  return report;
}

QVariantMap AgentEditController::openMemoryVaultInObsidian(bool dry_run) const {
  const auto root_path = agent_memory_root_directory(AppBootstrap::appDataLocation());
  const auto errors = ensure_agent_memory_vault_sections(root_path);
  const auto obsidian_url = obsidian_open_url_for_path(root_path);
  QVariantMap report{
      {QStringLiteral("ok"), errors.isEmpty()},
      {QStringLiteral("mode"), QStringLiteral("agent-memory-vault-open-obsidian")},
      {QStringLiteral("rootPath"), root_path},
      {QStringLiteral("obsidianUrl"), obsidian_url.toString(QUrl::FullyEncoded)},
      {QStringLiteral("fallback"), QStringLiteral("folder-open")},
      {QStringLiteral("dryRun"), dry_run},
      {QStringLiteral("errors"), errors},
  };
  if (!errors.isEmpty()) {
    report.insert(QStringLiteral("failureReason"), errors.join(QStringLiteral("; ")));
    return report;
  }
  if (dry_run) {
    report.insert(QStringLiteral("opened"), false);
    report.insert(QStringLiteral("fallbackUsed"), false);
    report.insert(QStringLiteral("deepLinkReady"), true);
    return report;
  }

  const auto opened = QDesktopServices::openUrl(obsidian_url);
  report.insert(QStringLiteral("opened"), opened);
  report.insert(QStringLiteral("deepLinkReady"), true);
  if (!opened) {
    const auto fallback_opened = QDesktopServices::openUrl(QUrl::fromLocalFile(root_path));
    report.insert(QStringLiteral("fallbackUsed"), true);
    report.insert(QStringLiteral("fallbackOpened"), fallback_opened);
    report.insert(QStringLiteral("ok"), fallback_opened);
    if (!fallback_opened) {
      report.insert(
          QStringLiteral("warnings"),
          QStringList{QStringLiteral("Unable to open the agent memory vault in Obsidian or the system file browser.")});
    }
  } else {
    report.insert(QStringLiteral("fallbackUsed"), false);
  }
  refreshMemoryVaultSnapshot();
  return report;
}

QVariantMap AgentEditController::resetMemoryVault(bool confirmed) {
  auto report = reset_agent_memory_vault(AppBootstrap::appDataLocation(), confirmed);
  if (report.value(QStringLiteral("ok")).toBool()) {
    last_run_report_.clear();
    run_history_.clear();
    refreshMemoryVaultSnapshot();
    emit lastRunReportChanged();
    emit runHistoryChanged();
  }
  return report;
}

QVariantMap AgentEditController::memoryPacketForPhase(const QString& phase) const {
  return agent_memory_packet_for_phase(
      AppBootstrap::appDataLocation(),
      selectedStyle(),
      last_run_report_,
      phase);
}

QVariantMap AgentEditController::aiEditWorkflowState(const QString& scope) const {
  const auto requested_scope = scope.trimmed().isEmpty() ? inferred_agent_scope(catalog_model_) : scope;
  const auto normalized_scope = normalized_agent_scope(requested_scope);
  const auto assets = scoped_asset_maps(catalog_model_, normalized_scope);
  const auto report_scope = report_scope_for_assets(normalized_scope, assets);
  const auto asset_count = static_cast<int>(assets.size());
  const auto style = selectedStyle();
  const auto style_learning = style_learning_candidate_summary(assets);
  const auto taste_card = style_taste_card_from_style(style);
  const auto active_style_learned = taste_card.value(QStringLiteral("learned")).toBool();
  const auto strong_style_learning_candidate =
      style_learning.value(QStringLiteral("available")).toBool() &&
      style_learning.value(QStringLiteral("editedReferenceCount")).toInt() > 0;
  const auto model = model_metadata_by_id(available_models_, selected_model_id_);
  const auto model_backend = model.value(QStringLiteral("backend")).toString();
  const auto model_available = model.value(QStringLiteral("available")).toBool();
  const auto model_configured = model.value(QStringLiteral("configured"), true).toBool();
  const auto model_configuration_ready =
      model_configured || model_backend == QStringLiteral("local");
  const auto model_status = model.value(QStringLiteral("status")).toString();
  const auto model_ready =
      !selected_model_id_.trimmed().isEmpty() &&
      !model_backend.trimmed().isEmpty() &&
      model_available &&
      model_configuration_ready;

  QStringList blocking_reasons;
  QString status_line = QStringLiteral("Ready");
  QString primary_key = QStringLiteral("create_samples");
  QString primary_label = asset_count > 1 ? QStringLiteral("Apply Edits")
                                          : QStringLiteral("Apply Edit");
  auto primary_enabled = true;
  auto setup_required = false;
  auto review_required = false;
  auto confirmation_required = false;
  const auto sample_review = last_run_report_.value(QStringLiteral("sampleReview")).toMap();
  const auto sample_remaining_asset_ids =
      sample_review.value(QStringLiteral("remainingAssetIds")).toStringList().isEmpty()
          ? last_run_report_.value(QStringLiteral("remainingAssetIds")).toStringList()
          : sample_review.value(QStringLiteral("remainingAssetIds")).toStringList();
  const auto sample_review_waiting =
      draftActive() &&
      last_run_report_.value(QStringLiteral("sampleOnlyDraft")).toBool() &&
      sample_review.value(QStringLiteral("available")).toBool() &&
      !sample_remaining_asset_ids.isEmpty();

  if (running_) {
    status_line = status_.trimmed().isEmpty() ? QStringLiteral("Working") : status_;
    if (sample_review_waiting &&
        (status_ == QStringLiteral("Applying draft") ||
         status_ == QStringLiteral("Planning draft"))) {
      status_line = QStringLiteral("Applying to batch");
    } else if (status_ == QStringLiteral("Planning draft") ||
               status_ == QStringLiteral("Applying draft")) {
      status_line = QStringLiteral("Creating samples");
    } else if (status_ == QStringLiteral("Preparing local AI") ||
               status_ == QStringLiteral("Installing model")) {
      status_line = QStringLiteral("Setting up local model");
    }
    primary_key = QStringLiteral("cancel");
    primary_label = QStringLiteral("Cancel");
  } else if (awaitingUserInput()) {
    status_line = QStringLiteral("Waiting for your answer");
    primary_key = QStringLiteral("answer_question");
    primary_label = QStringLiteral("Answer");
    primary_enabled = false;
    review_required = true;
  } else if (!safeStopReason().trimmed().isEmpty()) {
    status_line = QStringLiteral("Review needed");
    primary_key = QStringLiteral("review_issue");
    primary_label = QStringLiteral("Review Issues");
    review_required = true;
  } else if (sample_review_waiting) {
    status_line = QStringLiteral("Waiting for your review");
    primary_key = QStringLiteral("apply_to_all");
    primary_label = QStringLiteral("Apply to All");
    confirmation_required = true;
    review_required = true;
  } else if (draftActive()) {
    status_line = QStringLiteral("Ready to commit");
    primary_key = QStringLiteral("commit");
    primary_label = QStringLiteral("Commit Edits");
    confirmation_required = true;
  } else if (asset_count < 1) {
    status_line = QStringLiteral("Select photos");
    primary_key = QStringLiteral("select_photos");
    primary_label = QStringLiteral("Select Photos");
    primary_enabled = false;
    blocking_reasons.append(empty_scope_error(report_scope, QStringLiteral("AI editing")));
  } else if (style.isEmpty()) {
    status_line = QStringLiteral("Needs style");
    if (strong_style_learning_candidate) {
      primary_key = QStringLiteral("learn_style");
      primary_label = QStringLiteral("Learn This Style");
      primary_enabled = true;
    } else {
      primary_key = QStringLiteral("choose_style");
      primary_label = QStringLiteral("Choose Style");
      primary_enabled = false;
      blocking_reasons.append(QStringLiteral("Choose or learn a style before editing."));
    }
  } else if (modelInstallRunning()) {
    status_line = QStringLiteral("Setting up local model");
    primary_key = QStringLiteral("set_up_local_ai");
    primary_label = QStringLiteral("Setting Up");
    primary_enabled = false;
    setup_required = true;
  } else if (!model_ready) {
    status_line = QStringLiteral("Set up local AI");
    primary_key = QStringLiteral("set_up_local_ai");
    primary_label = QStringLiteral("Set Up Local AI");
    setup_required = true;
    if (model_backend.trimmed().isEmpty()) {
      blocking_reasons.append(QStringLiteral("No valid local model is selected."));
    } else if (!model_configuration_ready) {
      blocking_reasons.append(QStringLiteral("The selected local model is not configured."));
    } else if (!model_available) {
      const auto reason = model.value(QStringLiteral("statusReason")).toString();
      blocking_reasons.append(
          reason.trimmed().isEmpty()
              ? QStringLiteral("The selected local model has not passed a readiness check.")
              : reason.trimmed());
    }
  } else if (strong_style_learning_candidate && !active_style_learned) {
    status_line = QStringLiteral("Needs style");
    primary_key = QStringLiteral("learn_style");
    primary_label = QStringLiteral("Learn This Style");
  } else if (asset_count > 1) {
    status_line = QStringLiteral("Ready");
    primary_key = QStringLiteral("create_samples");
    primary_label = QStringLiteral("Apply Edits");
  }

  const QVariantMap primary_action{
      {QStringLiteral("key"), primary_key},
      {QStringLiteral("label"), primary_label},
      {QStringLiteral("enabled"), primary_enabled},
      {QStringLiteral("confirmationRequired"), confirmation_required},
  };
  const QVariantList secondary_actions{
      QVariantMap{
          {QStringLiteral("key"), QStringLiteral("change_style")},
          {QStringLiteral("label"), QStringLiteral("Change Style")},
          {QStringLiteral("enabled"), !running_},
      },
      QVariantMap{
          {QStringLiteral("key"), QStringLiteral("change_scope")},
          {QStringLiteral("label"), QStringLiteral("Change Scope")},
          {QStringLiteral("enabled"), !running_},
      },
      QVariantMap{
          {QStringLiteral("key"), QStringLiteral("add_feedback")},
          {QStringLiteral("label"), QStringLiteral("Add Feedback")},
          {QStringLiteral("enabled"), !running_ && !style.isEmpty()},
      },
      QVariantMap{
          {QStringLiteral("key"), QStringLiteral("rollback")},
          {QStringLiteral("label"), QStringLiteral("Roll Back")},
          {QStringLiteral("enabled"), !running_ && draftActive()},
      },
      QVariantMap{
          {QStringLiteral("key"), QStringLiteral("show_details")},
          {QStringLiteral("label"), QStringLiteral("Show Details")},
          {QStringLiteral("enabled"), !running_},
      },
  };
  const QVariantMap model_state{
      {QStringLiteral("id"), selected_model_id_},
      {QStringLiteral("name"), model.value(QStringLiteral("name"))},
      {QStringLiteral("backend"), model_backend},
      {QStringLiteral("status"), model_status},
      {QStringLiteral("available"), model_available},
      {QStringLiteral("configured"), model_configured},
      {QStringLiteral("ready"), model_ready},
      {QStringLiteral("installRunning"), modelInstallRunning()},
      {QStringLiteral("installStatus"), model_install_status_},
      {QStringLiteral("statusReason"), model.value(QStringLiteral("statusReason"))},
  };
  const QVariantMap style_state{
      {QStringLiteral("id"), selected_style_id_},
      {QStringLiteral("name"), style.value(QStringLiteral("name"))},
      {QStringLiteral("available"), !style.isEmpty()},
      {QStringLiteral("source"), style.value(QStringLiteral("source"))},
      {QStringLiteral("learned"), !style.value(QStringLiteral("learnedProfile")).toMap().isEmpty()},
      {QStringLiteral("tasteCard"), taste_card},
  };
  const QVariantMap mcp_state{
      {QStringLiteral("available"), mcp_available_},
      {QStringLiteral("status"), mcp_status_},
      {QStringLiteral("toolCount"), mcp_tool_names_.size()},
      {QStringLiteral("endpointKind"), mcp_endpoint_kind_},
  };

  QVariantMap report = result_map(blocking_reasons.isEmpty() && primary_enabled);
  report.insert(QStringLiteral("mode"), QStringLiteral("ai-edit-workflow-state"));
  report.insert(QStringLiteral("statusLine"), status_line);
  report.insert(QStringLiteral("primaryAction"), primary_action);
  report.insert(QStringLiteral("secondaryActions"), secondary_actions);
  report.insert(QStringLiteral("primaryActionKey"), primary_key);
  report.insert(QStringLiteral("primaryActionLabel"), primary_label);
  report.insert(QStringLiteral("primaryActionEnabled"), primary_enabled);
  report.insert(QStringLiteral("scope"), report_scope);
  report.insert(QStringLiteral("requestedScope"), normalized_scope);
  report.insert(QStringLiteral("inferredScope"), scope.trimmed().isEmpty());
  report.insert(QStringLiteral("scopeLabel"), ai_scope_label(report_scope, asset_count));
  report.insert(QStringLiteral("assetCount"), asset_count);
  report.insert(QStringLiteral("assetIds"), asset_ids_from_maps(assets));
  report.insert(QStringLiteral("sampleReview"), sample_review);
  report.insert(QStringLiteral("sampleReviewWaiting"), sample_review_waiting);
  report.insert(QStringLiteral("sampleRemainingAssetIds"), sample_remaining_asset_ids);
  report.insert(QStringLiteral("sampleRemainingCount"), sample_remaining_asset_ids.size());
  report.insert(QStringLiteral("style"), style_state);
  report.insert(QStringLiteral("styleLearning"), style_learning);
  report.insert(QStringLiteral("tasteCard"), taste_card);
  report.insert(QStringLiteral("model"), model_state);
  report.insert(QStringLiteral("mcp"), mcp_state);
  report.insert(QStringLiteral("setupRequired"), setup_required);
  report.insert(QStringLiteral("reviewRequired"), review_required);
  report.insert(QStringLiteral("draftActive"), draftActive());
  report.insert(QStringLiteral("awaitingUserInput"), awaitingUserInput());
  report.insert(QStringLiteral("safeStopReason"), safeStopReason());
  report.insert(QStringLiteral("blockingReasons"), blocking_reasons);
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
  clearPendingAgentInput();
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
    const QString& instructions,
    const QVariantMap& mcp_context) const {
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
  const auto scope_context =
      active_project_scope_context(catalog_model_, scope, scoped_assets);
  if (catalog_model_ != nullptr && scoped_assets.isEmpty()) {
    errors.append(empty_scope_error(scope, QStringLiteral("draft planning")));
  }
  if (!errors.isEmpty()) {
    return result_map(false, errors);
  }
  const auto representative_asset = scoped_assets.first().toMap();
  auto model_asset = representative_asset;
  QVariantMap planning_proof_report;
  const auto representative_recipe_json =
      representative_asset.value(QStringLiteral("recipeJson")).toString();
  const auto representative_recipe_validation =
      validate_edit_recipe_json(representative_recipe_json);
  if (representative_recipe_validation.valid) {
    const auto planning_run_id =
        mcp_context.value(QStringLiteral("runId")).toString().trimmed().isEmpty()
            ? QUuid::createUuid().toString(QUuid::WithoutBraces)
            : mcp_context.value(QStringLiteral("runId")).toString();
    planning_proof_report = render_agent_analysis_artifacts(
        representative_asset.value(QStringLiteral("filePath")).toString(),
        parse_edit_recipe_json(representative_recipe_json),
        style,
        planning_run_id,
        representative_asset.value(QStringLiteral("assetId")).toString(),
        QVariantMap{
            {QStringLiteral("ok"), true},
            {QStringLiteral("mode"), QStringLiteral("model-input-proof")},
            {QStringLiteral("scope"), report_scope},
            {QStringLiteral("assetId"), representative_asset.value(QStringLiteral("assetId"))},
        });
    const auto current_proof_path =
        planning_proof_report.value(QStringLiteral("currentProofPath")).toString();
    if (!current_proof_path.trimmed().isEmpty()) {
      model_asset.insert(QStringLiteral("proofImagePath"), current_proof_path);
      model_asset.insert(QStringLiteral("currentProofPath"), current_proof_path);
    }
  }
  const auto insert_planning_proof_metadata = [&](QVariantMap* report) {
    if (report == nullptr || planning_proof_report.isEmpty()) {
      return;
    }
    report->insert(QStringLiteral("planningProofReport"), planning_proof_report);
    report->insert(
        QStringLiteral("planningProofPath"),
        planning_proof_report.value(QStringLiteral("currentProofPath")));
    if (!report->contains(QStringLiteral("currentProofPath"))) {
      report->insert(
          QStringLiteral("currentProofPath"),
          planning_proof_report.value(QStringLiteral("currentProofPath")));
    }
  };

  AgentLocalModelPlanRequest request;
  request.model_id = selected_model_id_;
  request.style = style;
  request.asset = model_asset;
  request.current_recipe_json = representative_recipe_json;
  request.user_instructions = instructions.trimmed().isEmpty() ? user_instructions_ : instructions;
  request.available_tool_schemas =
      mcp_context.value(QStringLiteral("availableToolSchemas")).toList();
  request.prior_tool_results =
      mcp_context.value(QStringLiteral("priorToolResults")).toList();
  request.allow_tool_call_plan =
      mcp_context.value(QStringLiteral("allowToolCallPlan"), false).toBool();
  const auto mcp_tool_phase =
      mcp_context.value(QStringLiteral("toolPhase"), QStringLiteral("sample")).toString();
  request.strategy_phase = mcp_tool_phase;
  request.memory_packet = memoryPacketForPhase(mcp_tool_phase);
  const auto mcp_allowed_tools = mcp_context.value(QStringLiteral("allowedTools")).toStringList();
  const auto mcp_tool_budget = mcp_tool_budget_report(
      mcp_tool_phase,
      mcp_allowed_tools,
      static_cast<int>(request.available_tool_schemas.size()));
  auto worker_result = plan_agent_recipe_patch_on_worker(request);
  auto plan = worker_result.plan;
  QVariantMap model_tool_plan;
  QVariantList model_tool_plan_calls;
  if (plan.ok && plan.decision == QStringLiteral("tool_call_plan")) {
    model_tool_plan = execute_model_mcp_tool_plan(
        mcp_context.value(QStringLiteral("endpoint")).toString(),
        mcp_context.value(QStringLiteral("endpointKind")).toString(),
        mcp_context.value(QStringLiteral("runId")).toString(),
        mcp_context.value(QStringLiteral("allowedTools")).toStringList(),
        request.available_tool_schemas,
        plan.tool_call_plan,
        [this]() { return cancellation_requested_; });
    model_tool_plan_calls = mcp_model_tool_plan_call_records(model_tool_plan);
    auto follow_up_request = request;
    follow_up_request.allow_tool_call_plan = false;
    follow_up_request.prior_tool_results = model_tool_plan.value(QStringLiteral("toolReports")).toList();
    follow_up_request.user_instructions =
        QStringLiteral("%1\n\nMCP tool results JSON:\n%2")
            .arg(
                request.user_instructions,
                QString::fromUtf8(
                    QJsonDocument::fromVariant(follow_up_request.prior_tool_results)
                        .toJson(QJsonDocument::Compact)));
    worker_result = plan_agent_recipe_patch_on_worker(follow_up_request);
    plan = worker_result.plan;
  }
  if (!plan.ok) {
    auto failed = result_map(false, {plan.error_message});
    failed.insert(QStringLiteral("mcpToolPhase"), mcp_tool_phase);
    failed.insert(QStringLiteral("mcpAllowedToolNames"), mcp_allowed_tools);
    failed.insert(QStringLiteral("mcpAvailableToolCount"), request.available_tool_schemas.size());
    failed.insert(QStringLiteral("mcpToolBudget"), mcp_tool_budget);
    insert_planning_proof_metadata(&failed);
    if (!model_tool_plan.isEmpty()) {
      failed.insert(QStringLiteral("modelToolPlan"), model_tool_plan);
      failed.insert(QStringLiteral("modelToolPlanCalls"), model_tool_plan_calls);
    }
    return failed;
  }
  if (plan.decision == QStringLiteral("tool_call_plan")) {
    auto failed = result_map(false, {QStringLiteral("The local model requested tool calls after the allowed tool-call round.")});
    failed.insert(QStringLiteral("mcpToolPhase"), mcp_tool_phase);
    failed.insert(QStringLiteral("mcpAllowedToolNames"), mcp_allowed_tools);
    failed.insert(QStringLiteral("mcpAvailableToolCount"), request.available_tool_schemas.size());
    failed.insert(QStringLiteral("mcpToolBudget"), mcp_tool_budget);
    failed.insert(QStringLiteral("modelToolPlan"), model_tool_plan);
    failed.insert(QStringLiteral("modelToolPlanCalls"), model_tool_plan_calls);
    return failed;
  }
  if (plan.decision == QStringLiteral("needs_user_input") ||
      plan.decision == QStringLiteral("cannot_complete_safely")) {
    auto stopped = result_map(false, {plan.plan_summary}, plan.warnings);
    stopped.insert(QStringLiteral("decision"), plan.decision);
    stopped.insert(QStringLiteral("modelId"), plan.model_id);
    stopped.insert(QStringLiteral("styleId"), selected_style_id_);
    stopped.insert(QStringLiteral("assetId"), representative_asset.value(QStringLiteral("assetId")));
    stopped.insert(QStringLiteral("scope"), report_scope);
    stopped.insert(QStringLiteral("planSummary"), plan.plan_summary);
    stopped.insert(QStringLiteral("needsUserInput"), plan.decision == QStringLiteral("needs_user_input"));
    stopped.insert(
        QStringLiteral("cannotCompleteSafely"),
        plan.decision == QStringLiteral("cannot_complete_safely"));
    stopped.insert(QStringLiteral("safeStopReason"), plan.safe_stop_reason);
    stopped.insert(QStringLiteral("userQuestions"), plan.user_questions);
    stopped.insert(QStringLiteral("modelPromptAudit"), plan.prompt_audit);
    stopped.insert(QStringLiteral("modelRunnerMode"), QStringLiteral("worker-thread"));
    stopped.insert(QStringLiteral("uiEventPumpCount"), worker_result.ui_event_pump_count);
    stopped.insert(QStringLiteral("mcpToolPhase"), mcp_tool_phase);
    stopped.insert(QStringLiteral("mcpAllowedToolNames"), mcp_allowed_tools);
    stopped.insert(QStringLiteral("mcpAvailableToolCount"), request.available_tool_schemas.size());
    stopped.insert(QStringLiteral("mcpToolBudget"), mcp_tool_budget);
    stopped.insert(QStringLiteral("resumeScope"), report_scope);
    stopped.insert(QStringLiteral("resumeInstructions"), request.user_instructions);
    insert_planning_proof_metadata(&stopped);
    if (!model_tool_plan.isEmpty()) {
      stopped.insert(QStringLiteral("modelToolPlan"), model_tool_plan);
      stopped.insert(QStringLiteral("modelToolPlanCalls"), model_tool_plan_calls);
    }
    return stopped;
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
  insert_planning_proof_metadata(&report);
  report.insert(QStringLiteral("mcpToolPhase"), mcp_tool_phase);
  report.insert(QStringLiteral("mcpAllowedToolNames"), mcp_allowed_tools);
  report.insert(QStringLiteral("mcpAvailableToolCount"), request.available_tool_schemas.size());
  report.insert(QStringLiteral("mcpToolBudget"), mcp_tool_budget);
  report.insert(QStringLiteral("decision"), plan.decision);
  report.insert(QStringLiteral("modelId"), plan.model_id);
  report.insert(QStringLiteral("modelPromptAudit"), plan.prompt_audit);
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
            plan.recipe_patch,
            scope_context));
  }
  report.insert(QStringLiteral("styleConstraintCitations"), plan.style_constraint_citations);
  report.insert(QStringLiteral("recipePatch"), plan.recipe_patch);
  report.insert(QStringLiteral("recipeBaseMode"), recipe_base_mode_);
  report.insert(QStringLiteral("manualEditPolicy"), manual_edit_policy_for_base_mode(recipe_base_mode_));
  report.insert(QStringLiteral("modelRunnerMode"), QStringLiteral("worker-thread"));
  report.insert(QStringLiteral("uiEventPumpCount"), worker_result.ui_event_pump_count);
  if (!model_tool_plan.isEmpty()) {
    report.insert(QStringLiteral("modelToolPlan"), model_tool_plan);
    report.insert(QStringLiteral("modelToolPlanCalls"), model_tool_plan_calls);
  }
  return report;
}

QVariantMap AgentEditController::runDraftOnSelected(const QString& instructions) {
  return runDraftForScope(QStringLiteral("selected-assets"), instructions);
}

QVariantMap AgentEditController::applyEditForSelected(const QString& instructions) {
  return applyEditForScope(QStringLiteral("selected-assets"), instructions);
}

QVariantMap AgentEditController::applyEditForScope(
    const QString& scope,
    const QString& instructions) {
  const auto requested_scope = scope.trimmed().isEmpty() ? inferred_agent_scope(catalog_model_) : scope;
  auto draft = runDraftForScope(requested_scope, instructions);
  if (!draft.value(QStringLiteral("ok")).toBool()) {
    const auto draft_report = draft;
    draft.insert(QStringLiteral("mode"), QStringLiteral("apply-edit"));
    draft.insert(QStringLiteral("draftReport"), draft_report);
    draft.insert(QStringLiteral("committed"), false);
    draft.insert(QStringLiteral("commitSkippedReason"), QStringLiteral("draft_not_ready"));
    setLastRunReport(draft);
    return draft;
  }

  const auto sample_review = draft.value(QStringLiteral("sampleReview")).toMap();
  const auto sample_remaining_asset_ids =
      sample_review.value(QStringLiteral("remainingAssetIds")).toStringList().isEmpty()
          ? draft.value(QStringLiteral("remainingAssetIds")).toStringList()
          : sample_review.value(QStringLiteral("remainingAssetIds")).toStringList();
  if (draft.value(QStringLiteral("sampleOnlyDraft")).toBool() ||
      (sample_review.value(QStringLiteral("available")).toBool() &&
       !sample_remaining_asset_ids.isEmpty())) {
    const auto draft_report = draft;
    draft.insert(QStringLiteral("mode"), QStringLiteral("apply-edit"));
    draft.insert(QStringLiteral("draftReport"), draft_report);
    draft.insert(QStringLiteral("committed"), false);
    draft.insert(QStringLiteral("commitSkippedReason"), QStringLiteral("sample_review_required"));
    setStatus(QStringLiteral("Waiting for your review"));
    setLastRunReport(draft);
    return draft;
  }

  if (!draftActive()) {
    const auto draft_report = draft;
    draft.insert(QStringLiteral("mode"), QStringLiteral("apply-edit"));
    draft.insert(QStringLiteral("draftReport"), draft_report);
    draft.insert(QStringLiteral("committed"), false);
    draft.insert(QStringLiteral("commitSkippedReason"), QStringLiteral("no_active_draft"));
    setLastRunReport(draft);
    return draft;
  }

  const auto commit = commitDraft(true);
  QVariantMap report = draft;
  report.insert(QStringLiteral("mode"), QStringLiteral("apply-edit"));
  report.insert(QStringLiteral("draftReport"), draft);
  report.insert(QStringLiteral("commitReport"), commit);
  report.insert(QStringLiteral("committed"), commit.value(QStringLiteral("ok")).toBool());
  report.insert(QStringLiteral("committedCount"), commit.value(QStringLiteral("committedCount")));
  report.insert(QStringLiteral("draftActive"), draftActive());
  if (!commit.value(QStringLiteral("ok")).toBool()) {
    auto errors = draft.value(QStringLiteral("errors")).toStringList();
    errors.append(commit.value(QStringLiteral("errors")).toStringList());
    report.insert(QStringLiteral("ok"), false);
    report.insert(QStringLiteral("errors"), errors);
    if (!errors.isEmpty()) {
      report.insert(QStringLiteral("failureReason"), errors.join(QStringLiteral("; ")));
    }
    setStatus(QStringLiteral("Failed"));
    setErrorMessage(report.value(QStringLiteral("failureReason")).toString());
  } else {
    report.insert(QStringLiteral("ok"), true);
    setStatus(QStringLiteral("Applied"));
    setErrorMessage(QString());
  }
  setLastRunReport(report);
  return report;
}

QVariantMap AgentEditController::answerPendingUserInput(const QString& answer) {
  const auto trimmed_answer = answer.trimmed();
  if (pending_agent_input_.isEmpty() || !awaitingUserInput()) {
    return result_map(false, {QStringLiteral("No agent question is waiting for an answer.")});
  }
  if (trimmed_answer.isEmpty()) {
    return result_map(false, {QStringLiteral("Answer text is required.")});
  }
  const auto scope = pending_agent_input_
                         .value(QStringLiteral("resumeScope"), QStringLiteral("selected-assets"))
                         .toString();
  const auto original_instructions =
      pending_agent_input_.value(QStringLiteral("resumeInstructions")).toString();
  const auto resumed_instructions =
      QStringLiteral("%1\n\nUser answer to agent question:\n%2")
          .arg(original_instructions.trimmed(), trimmed_answer);
  clearPendingAgentInput();
  return runDraftForScope(scope, resumed_instructions);
}

QVariantMap AgentEditController::dismissPendingAgentInput() {
  if (pending_agent_input_.isEmpty()) {
    return result_map(false, {QStringLiteral("No pending agent input is available.")});
  }
  clearPendingAgentInput();
  QVariantMap report = result_map(true);
  report.insert(QStringLiteral("mode"), QStringLiteral("agent-input-dismissed"));
  setStatus(QStringLiteral("Idle"));
  return report;
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
  clearPendingAgentInput();
  setRunning(true);
  setErrorMessage(QString());
  setStatus(QStringLiteral("Planning draft"));
  rollback_on_cancel_ = false;
  QElapsedTimer mcp_timer;
  mcp_timer.start();
  auto mcp_discovery = ensureMcpReady(QStringLiteral("draft-run"), 3);
  mcp_discovery.insert(QStringLiteral("durationMs"), mcp_timer.elapsed());
  const auto mcp_phase_asset_count = scoped_asset_maps(catalog_model_, scope).size();
  const auto mcp_tool_phase =
      mcp_phase_asset_count > 1 ? QStringLiteral("batch") : QStringLiteral("sample");
  const auto mcp_allowed_tool_names = mcp_tools_for_agent_phase(mcp_tool_phase);
  const auto mcp_available_tool_schemas =
      filter_mcp_tools_for_phase(mcp_discovery.value(QStringLiteral("tools")).toList(), mcp_tool_phase);
  const auto mcp_repair_available_tool_schemas =
      filter_mcp_tools_for_phase(mcp_discovery.value(QStringLiteral("tools")).toList(), QStringLiteral("revision"));
  mcp_discovery.insert(QStringLiteral("toolPhase"), mcp_tool_phase);
  mcp_discovery.insert(QStringLiteral("phaseAllowedToolNames"), mcp_allowed_tool_names);
  mcp_discovery.insert(QStringLiteral("phaseAvailableToolSchemas"), mcp_available_tool_schemas);
  mcp_discovery.insert(QStringLiteral("phaseAvailableToolCount"), mcp_available_tool_schemas.size());
  QElapsedTimer mcp_observation_timer;
  mcp_observation_timer.start();
  auto mcp_observation = observe_mcp_tools(
      mcp_endpoint_,
      mcp_endpoint_kind_,
      QStringLiteral("agent-run-observe"),
      [this]() { return cancellation_requested_; });
  mcp_observation.insert(QStringLiteral("durationMs"), mcp_observation_timer.elapsed());
  const auto mcp_observation_tool_calls = mcp_observation_tool_call_records(mcp_observation);
  const QVariantMap mcp_planning_context{
      {QStringLiteral("endpoint"), mcp_endpoint_},
      {QStringLiteral("endpointKind"), mcp_endpoint_kind_},
      {QStringLiteral("runId"), QStringLiteral("agent-run-model-tool-plan")},
      {QStringLiteral("toolPhase"), mcp_tool_phase},
      {QStringLiteral("allowedTools"), mcp_allowed_tool_names},
      {QStringLiteral("allowToolCallPlan"),
       mcp_discovery.value(QStringLiteral("ready")).toBool() && !mcp_available_tool_schemas.isEmpty()},
      {QStringLiteral("availableToolSchemas"), mcp_available_tool_schemas},
      {QStringLiteral("priorToolResults"), mcp_observation.value(QStringLiteral("toolReports"))},
  };
  QElapsedTimer plan_timer;
  plan_timer.start();
  auto plan = planDraftForScope(scope, instructions, mcp_planning_context);
  plan.insert(QStringLiteral("mcpDiscovery"), mcp_discovery);
  plan.insert(QStringLiteral("mcpObservation"), mcp_observation);
  plan.insert(QStringLiteral("mcpAttached"), mcp_discovery.value(QStringLiteral("ready")).toBool());
  plan.insert(QStringLiteral("mcpToolCount"), mcp_discovery.value(QStringLiteral("toolCount")));
  plan.insert(QStringLiteral("mcpToolNames"), mcp_discovery.value(QStringLiteral("toolNames")));
  plan.insert(QStringLiteral("mcpToolPhase"), mcp_tool_phase);
  plan.insert(QStringLiteral("mcpAllowedToolNames"), mcp_allowed_tool_names);
  plan.insert(QStringLiteral("mcpAvailableToolCount"), mcp_available_tool_schemas.size());
  plan.insert(QStringLiteral("mcpEndpointKind"), mcp_discovery.value(QStringLiteral("endpointKind")));
  if (!plan.value(QStringLiteral("ok")).toBool()) {
    const auto errors = plan.value(QStringLiteral("errors")).toStringList();
    QVariantList failure_tool_calls{tool_call_record(
        QStringLiteral("mcp.tools/list"),
        mcp_discovery.value(QStringLiteral("ready")).toBool() ? QStringLiteral("ok") : QStringLiteral("unavailable"),
        QVariantMap{
            {QStringLiteral("endpointKind"), mcp_discovery.value(QStringLiteral("endpointKind"))},
            {QStringLiteral("toolCount"), mcp_discovery.value(QStringLiteral("toolCount"))},
            {QStringLiteral("durationMs"), mcp_discovery.value(QStringLiteral("durationMs"))},
            {QStringLiteral("available"), mcp_discovery.value(QStringLiteral("available"))},
        })};
    failure_tool_calls.append(mcp_observation_tool_calls);
    failure_tool_calls.append(plan.value(QStringLiteral("modelToolPlanCalls")).toList());
    plan.insert(QStringLiteral("toolCalls"), failure_tool_calls);
    const auto run_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto model_id = plan.value(QStringLiteral("modelId"), selected_model_id_).toString();
    const auto model = model_metadata_by_id(available_models_, model_id);
    plan.insert(QStringLiteral("mode"), QStringLiteral("draft"));
    plan.insert(QStringLiteral("runId"), run_id);
    if (!plan.contains(QStringLiteral("scope"))) {
      plan.insert(QStringLiteral("scope"), normalized_agent_scope(scope));
    }
    plan.insert(QStringLiteral("styleId"), selected_style_id_);
    plan.insert(QStringLiteral("modelId"), model_id);
    plan.insert(QStringLiteral("modelBackend"), model.value(QStringLiteral("backend")));
    plan.insert(QStringLiteral("draftActive"), draftActive());
    finalize_timing(&plan, started_at_utc, run_timer.elapsed(), failure_tool_calls);
    write_agent_run_manifest(&plan, selectedStyle(), model, model_id);
    setRunning(false);
    const auto decision = plan.value(QStringLiteral("decision")).toString();
    setStatus(decision == QStringLiteral("needs_user_input")
                  ? QStringLiteral("Needs user input")
                  : decision == QStringLiteral("cannot_complete_safely")
                        ? QStringLiteral("Stopped safely")
                        : QStringLiteral("Failed"));
    if (decision == QStringLiteral("needs_user_input") ||
        decision == QStringLiteral("cannot_complete_safely")) {
      setPendingAgentInput(plan);
    }
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
    const auto mcp_tool_call = tool_call_record(
        QStringLiteral("mcp.tools/list"),
        mcp_discovery.value(QStringLiteral("ready")).toBool() ? QStringLiteral("ok") : QStringLiteral("unavailable"),
        QVariantMap{
            {QStringLiteral("endpointKind"), mcp_discovery.value(QStringLiteral("endpointKind"))},
            {QStringLiteral("toolCount"), mcp_discovery.value(QStringLiteral("toolCount"))},
            {QStringLiteral("durationMs"), mcp_discovery.value(QStringLiteral("durationMs"))},
            {QStringLiteral("available"), mcp_discovery.value(QStringLiteral("available"))},
        });
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
    QVariantList tool_calls{mcp_tool_call};
    tool_calls.append(mcp_observation_tool_calls);
    tool_calls.append(plan.value(QStringLiteral("modelToolPlanCalls")).toList());
    tool_calls.append(plan_tool_call);
    canceled.insert(QStringLiteral("mode"), QStringLiteral("draft"));
    canceled.insert(QStringLiteral("runId"), draft_run_id_);
    canceled.insert(QStringLiteral("scope"), report_scope);
    canceled.insert(QStringLiteral("assetIds"), target_asset_ids);
    canceled.insert(QStringLiteral("cancellationReason"), cancellation_reason);
    canceled.insert(QStringLiteral("styleId"), selected_style_id_);
    canceled.insert(QStringLiteral("modelId"), plan.value(QStringLiteral("modelId")));
    canceled.insert(QStringLiteral("modelBackend"), model.value(QStringLiteral("backend")));
    canceled.insert(QStringLiteral("modelPromptAudit"), plan.value(QStringLiteral("modelPromptAudit")));
    canceled.insert(QStringLiteral("mcpDiscovery"), mcp_discovery);
    canceled.insert(QStringLiteral("mcpObservation"), mcp_observation);
    canceled.insert(QStringLiteral("mcpAttached"), mcp_discovery.value(QStringLiteral("ready")).toBool());
    canceled.insert(QStringLiteral("mcpToolCount"), mcp_discovery.value(QStringLiteral("toolCount")));
    canceled.insert(QStringLiteral("mcpToolNames"), mcp_discovery.value(QStringLiteral("toolNames")));
    canceled.insert(QStringLiteral("mcpEndpointKind"), mcp_discovery.value(QStringLiteral("endpointKind")));
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
      request.strategy_phase = QStringLiteral("batch");
      request.memory_packet = memoryPacketForPhase(QStringLiteral("batch"));
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
      cluster_plan_report.insert(QStringLiteral("modelPromptAudit"), cluster_plan.prompt_audit);
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
      const auto run_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
      const auto report_scope = report_scope_for_assets(scope, scoped_assets);
      const auto target_asset_ids = asset_ids_from_maps(scoped_assets);
      const auto model = model_metadata_by_id(available_models_, selected_model_id_);
      QVariantList failure_tool_calls{tool_call_record(
          QStringLiteral("mcp.tools/list"),
          mcp_discovery.value(QStringLiteral("ready")).toBool() ? QStringLiteral("ok") : QStringLiteral("unavailable"),
          QVariantMap{
              {QStringLiteral("endpointKind"), mcp_discovery.value(QStringLiteral("endpointKind"))},
              {QStringLiteral("toolCount"), mcp_discovery.value(QStringLiteral("toolCount"))},
              {QStringLiteral("durationMs"), mcp_discovery.value(QStringLiteral("durationMs"))},
              {QStringLiteral("available"), mcp_discovery.value(QStringLiteral("available"))},
          })};
      failure_tool_calls.append(mcp_observation_tool_calls);
      failed_plan.insert(QStringLiteral("mode"), QStringLiteral("draft"));
      failed_plan.insert(QStringLiteral("runId"), run_id);
      failed_plan.insert(QStringLiteral("scope"), report_scope);
      failed_plan.insert(QStringLiteral("assetIds"), target_asset_ids);
      failed_plan.insert(QStringLiteral("styleId"), selected_style_id_);
      failed_plan.insert(QStringLiteral("modelId"), selected_model_id_);
      failed_plan.insert(QStringLiteral("modelBackend"), model.value(QStringLiteral("backend")));
      failed_plan.insert(QStringLiteral("mcpDiscovery"), mcp_discovery);
      failed_plan.insert(QStringLiteral("mcpObservation"), mcp_observation);
      failed_plan.insert(QStringLiteral("mcpAttached"), mcp_discovery.value(QStringLiteral("ready")).toBool());
      failed_plan.insert(QStringLiteral("mcpToolCount"), mcp_discovery.value(QStringLiteral("toolCount")));
      failed_plan.insert(QStringLiteral("mcpToolNames"), mcp_discovery.value(QStringLiteral("toolNames")));
      failed_plan.insert(QStringLiteral("mcpEndpointKind"), mcp_discovery.value(QStringLiteral("endpointKind")));
      failed_plan.insert(QStringLiteral("clusterPlanning"), cluster_plans);
      failed_plan.insert(QStringLiteral("batchClusters"), batch_plan_metadata.value(QStringLiteral("batchClusters")));
      failed_plan.insert(QStringLiteral("representativeAssetIds"), batch_plan_metadata.value(QStringLiteral("representativeAssetIds")));
      failed_plan.insert(
          QStringLiteral("batchSummary"),
          batch_summary(report_scope, target_asset_ids, {}, false, false));
      finalize_timing(&failed_plan, started_at_utc, run_timer.elapsed(), failure_tool_calls);
      write_agent_run_manifest(&failed_plan, selectedStyle(), model, selected_model_id_);
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
      plan.insert(QStringLiteral("modelPromptAudit"), first_cluster_plan.value(QStringLiteral("modelPromptAudit")));
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
    canceled.insert(QStringLiteral("modelPromptAudit"), plan.value(QStringLiteral("modelPromptAudit")));
    canceled.insert(QStringLiteral("mcpDiscovery"), mcp_discovery);
    canceled.insert(QStringLiteral("mcpObservation"), mcp_observation);
    canceled.insert(QStringLiteral("mcpAttached"), mcp_discovery.value(QStringLiteral("ready")).toBool());
    canceled.insert(QStringLiteral("mcpToolCount"), mcp_discovery.value(QStringLiteral("toolCount")));
    canceled.insert(QStringLiteral("mcpToolNames"), mcp_discovery.value(QStringLiteral("toolNames")));
    canceled.insert(QStringLiteral("mcpEndpointKind"), mcp_discovery.value(QStringLiteral("endpointKind")));
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
  batch_plan_metadata.insert(
      QStringLiteral("modelRepairContext"),
      QVariantMap{
          {QStringLiteral("modelId"), plan.value(QStringLiteral("modelId"))},
          {QStringLiteral("userInstructions"), instructions.trimmed().isEmpty() ? user_instructions_ : instructions},
          {QStringLiteral("availableToolSchemas"), mcp_repair_available_tool_schemas},
          {QStringLiteral("toolPhase"), QStringLiteral("revision")},
          {QStringLiteral("memoryPacket"), memoryPacketForPhase(QStringLiteral("revision"))},
      });
  batch_plan_metadata.insert(QStringLiteral("clusterPlanning"), cluster_plans);
  const auto full_target_asset_ids = asset_ids_from_maps(scoped_assets);
  const auto representative_asset_ids =
      batch_plan_metadata.value(QStringLiteral("representativeAssetIds")).toStringList();
  batch_plan_metadata.insert(QStringLiteral("fullTargetAssetIds"), full_target_asset_ids);
  if (!patches_by_asset_id.isEmpty()) {
    batch_plan_metadata.insert(
        QStringLiteral("plannedPatchesByAssetId"),
        patches_by_asset_id_to_variant_map(patches_by_asset_id));
  }
  if (scoped_assets.size() >= 3 &&
      !representative_asset_ids.isEmpty() &&
      representative_asset_ids.size() < full_target_asset_ids.size()) {
    const auto remaining_asset_ids =
        ordered_difference(full_target_asset_ids, representative_asset_ids);
    batch_plan_metadata.insert(QStringLiteral("sampleOnlyAssetIds"), representative_asset_ids);
    batch_plan_metadata.insert(QStringLiteral("remainingAssetIds"), remaining_asset_ids);
    batch_plan_metadata.insert(QStringLiteral("sampleOnlyDraft"), true);
    batch_plan_metadata.insert(
        QStringLiteral("sampleDraftPolicy"),
        QStringLiteral("representative-cluster-samples-before-full-batch"));
    plan.insert(
        QStringLiteral("planSummary"),
        QStringLiteral("%1 Creating %2 representative sample%3 first; %4 remaining photo%5 will wait for approval.")
            .arg(plan.value(QStringLiteral("planSummary")).toString())
            .arg(representative_asset_ids.size())
            .arg(representative_asset_ids.size() == 1 ? QString() : QStringLiteral("s"))
            .arg(remaining_asset_ids.size())
            .arg(remaining_asset_ids.size() == 1 ? QString() : QStringLiteral("s")));
  }

  QVariantMap representative_asset;
  const auto representative_asset_id =
      plan.value(QStringLiteral("representativeAssetId"),
                 plan.value(QStringLiteral("assetId")))
          .toString();
  for (const auto& asset_value : scoped_assets) {
    const auto asset = asset_value.toMap();
    if (representative_asset.isEmpty() ||
        asset.value(QStringLiteral("assetId")).toString() == representative_asset_id) {
      representative_asset = asset;
    }
    if (asset.value(QStringLiteral("assetId")).toString() == representative_asset_id) {
      break;
    }
  }
  const auto report_scope = report_scope_for_assets(scope, scoped_assets);
  const auto target_asset_ids = asset_ids_from_maps(scoped_assets);
  const auto recipe_validation =
      validate_edit_recipe_json(representative_asset.value(QStringLiteral("recipeJson")).toString());
  if (recipe_validation.valid) {
    auto state =
        parse_edit_recipe_json(representative_asset.value(QStringLiteral("recipeJson")).toString());
    plan.insert(
        QStringLiteral("riskAssessment"),
        agent_risk_assessment(
            state,
            plan_warnings,
            plan.value(QStringLiteral("recipePatch")).toMap(),
            active_project_scope_context(catalog_model_, scope, scoped_assets)));
  }
  const auto stop_policy = pre_draft_stop_policy(plan);
  if (stop_policy.value(QStringLiteral("stopRequired")).toBool()) {
    const auto model_id = plan.value(QStringLiteral("modelId"), selected_model_id_).toString();
    const auto model = model_metadata_by_id(available_models_, model_id);
    const auto run_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto mcp_tool_call = tool_call_record(
        QStringLiteral("mcp.tools/list"),
        mcp_discovery.value(QStringLiteral("ready")).toBool() ? QStringLiteral("ok") : QStringLiteral("unavailable"),
        QVariantMap{
            {QStringLiteral("endpointKind"), mcp_discovery.value(QStringLiteral("endpointKind"))},
            {QStringLiteral("toolCount"), mcp_discovery.value(QStringLiteral("toolCount"))},
            {QStringLiteral("durationMs"), mcp_discovery.value(QStringLiteral("durationMs"))},
            {QStringLiteral("available"), mcp_discovery.value(QStringLiteral("available"))},
        });
    const auto plan_tool_call = tool_call_record(
        QStringLiteral("plan_agent_recipe_patch"),
        QStringLiteral("needs_user_input"),
        QVariantMap{
            {QStringLiteral("modelId"), model_id},
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
    const auto safety_gate_tool_call = tool_call_record(
        QStringLiteral("pre_draft_safety_gate"),
        QStringLiteral("needs_user_input"),
        QVariantMap{
            {QStringLiteral("triggers"), stop_policy.value(QStringLiteral("triggers"))},
            {QStringLiteral("approvalTypes"), stop_policy.value(QStringLiteral("approvalTypes"))},
            {QStringLiteral("confidence"), stop_policy.value(QStringLiteral("confidence"))},
        });
    QVariantList tool_calls{mcp_tool_call};
    tool_calls.append(mcp_observation_tool_calls);
    tool_calls.append(plan.value(QStringLiteral("modelToolPlanCalls")).toList());
    tool_calls.append(plan_tool_call);
    tool_calls.append(safety_gate_tool_call);
    QVariantMap stopped = result_map(
        false,
        {QStringLiteral("Agent needs approval before creating this draft.")},
        plan_warnings);
    stopped.insert(QStringLiteral("mode"), QStringLiteral("draft"));
    stopped.insert(QStringLiteral("runId"), run_id);
    stopped.insert(QStringLiteral("decision"), QStringLiteral("needs_user_input"));
    stopped.insert(QStringLiteral("needsUserInput"), true);
    stopped.insert(QStringLiteral("cannotCompleteSafely"), false);
    stopped.insert(QStringLiteral("safeStopReason"), QStringLiteral("high_risk_or_low_confidence"));
    stopped.insert(
        QStringLiteral("userQuestions"),
        QVariantList{QStringLiteral("Review and approve this higher-risk plan before the agent creates an editable draft?")});
    stopped.insert(QStringLiteral("scope"), report_scope);
    stopped.insert(QStringLiteral("assetIds"), target_asset_ids);
    stopped.insert(QStringLiteral("assetId"), plan.value(QStringLiteral("assetId")));
    stopped.insert(QStringLiteral("representativeAssetId"), plan.value(QStringLiteral("representativeAssetId")));
    stopped.insert(QStringLiteral("styleId"), selected_style_id_);
    stopped.insert(QStringLiteral("modelId"), model_id);
    stopped.insert(QStringLiteral("modelBackend"), model.value(QStringLiteral("backend")));
    stopped.insert(QStringLiteral("modelPromptAudit"), plan.value(QStringLiteral("modelPromptAudit")));
    stopped.insert(QStringLiteral("planSummary"), plan.value(QStringLiteral("planSummary")));
    stopped.insert(QStringLiteral("editGoals"), plan.value(QStringLiteral("editGoals")));
    stopped.insert(QStringLiteral("riskAssessment"), plan.value(QStringLiteral("riskAssessment")));
    stopped.insert(QStringLiteral("preDraftStopPolicy"), stop_policy);
    stopped.insert(QStringLiteral("recipePatch"), plan.value(QStringLiteral("recipePatch")));
    stopped.insert(QStringLiteral("plannedRecipePatch"), plan.value(QStringLiteral("recipePatch")));
    stopped.insert(QStringLiteral("styleConstraintCitations"), plan.value(QStringLiteral("styleConstraintCitations")));
    stopped.insert(QStringLiteral("mcpDiscovery"), mcp_discovery);
    stopped.insert(QStringLiteral("mcpObservation"), mcp_observation);
    stopped.insert(QStringLiteral("mcpAttached"), mcp_discovery.value(QStringLiteral("ready")).toBool());
    stopped.insert(QStringLiteral("mcpToolCount"), mcp_discovery.value(QStringLiteral("toolCount")));
    stopped.insert(QStringLiteral("mcpToolNames"), mcp_discovery.value(QStringLiteral("toolNames")));
    stopped.insert(QStringLiteral("mcpToolPhase"), plan.value(QStringLiteral("mcpToolPhase")));
    stopped.insert(QStringLiteral("mcpAllowedToolNames"), plan.value(QStringLiteral("mcpAllowedToolNames")));
    stopped.insert(QStringLiteral("mcpAvailableToolCount"), plan.value(QStringLiteral("mcpAvailableToolCount")));
    stopped.insert(QStringLiteral("mcpToolBudget"), plan.value(QStringLiteral("mcpToolBudget")));
    stopped.insert(QStringLiteral("mcpEndpointKind"), mcp_discovery.value(QStringLiteral("endpointKind")));
    stopped.insert(QStringLiteral("clusterPlanning"), cluster_plans);
    stopped.insert(QStringLiteral("batchClusters"), batch_plan_metadata.value(QStringLiteral("batchClusters")));
    stopped.insert(QStringLiteral("representativeAssetIds"), batch_plan_metadata.value(QStringLiteral("representativeAssetIds")));
    stopped.insert(QStringLiteral("resumeScope"), report_scope);
    stopped.insert(QStringLiteral("resumeInstructions"), instructions.trimmed().isEmpty() ? user_instructions_ : instructions);
    stopped.insert(
        QStringLiteral("batchSummary"),
        batch_summary(report_scope, target_asset_ids, {}, false, false));
    stopped.insert(QStringLiteral("toolCalls"), tool_calls);
    finalize_timing(&stopped, started_at_utc, run_timer.elapsed(), tool_calls);
    log_agent_tool_calls(run_id, tool_calls);
    write_agent_run_manifest(&stopped, selectedStyle(), model, model_id);
    setRunning(false);
    setStatus(QStringLiteral("Needs user input"));
    setErrorMessage(stopped.value(QStringLiteral("failureReason")).toString());
    setPendingAgentInput(stopped);
    setLastRunReport(stopped);
    return stopped;
  }
  auto draft = applyDraftRecipePatchesForScope(
      scope,
      plan.value(QStringLiteral("recipePatch")).toMap(),
      patches_by_asset_id,
      batch_plan_metadata);
  const auto model = model_metadata_by_id(available_models_, plan.value(QStringLiteral("modelId")).toString());
  auto tool_calls = report_tool_calls(draft);
  const auto model_tool_plan_calls = plan.value(QStringLiteral("modelToolPlanCalls")).toList();
  const auto mcp_tool_call = tool_call_record(
      QStringLiteral("mcp.tools/list"),
      mcp_discovery.value(QStringLiteral("ready")).toBool() ? QStringLiteral("ok") : QStringLiteral("unavailable"),
      QVariantMap{
          {QStringLiteral("endpointKind"), mcp_discovery.value(QStringLiteral("endpointKind"))},
          {QStringLiteral("toolCount"), mcp_discovery.value(QStringLiteral("toolCount"))},
          {QStringLiteral("durationMs"), mcp_discovery.value(QStringLiteral("durationMs"))},
          {QStringLiteral("available"), mcp_discovery.value(QStringLiteral("available"))},
      });
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
  for (auto index = model_tool_plan_calls.size() - 1; index >= 0; --index) {
    tool_calls.prepend(model_tool_plan_calls.at(index));
  }
  for (auto index = mcp_observation_tool_calls.size() - 1; index >= 0; --index) {
    tool_calls.prepend(mcp_observation_tool_calls.at(index));
  }
  tool_calls.prepend(mcp_tool_call);
  QVariantList planning_tool_calls{mcp_tool_call};
  planning_tool_calls.append(mcp_observation_tool_calls);
  planning_tool_calls.append(model_tool_plan_calls);
  planning_tool_calls.append(plan_tool_call);
  log_agent_tool_calls(draft.value(QStringLiteral("runId")).toString(), planning_tool_calls);
  draft.insert(QStringLiteral("mode"), QStringLiteral("draft"));
  draft.insert(QStringLiteral("modelId"), plan.value(QStringLiteral("modelId")));
  draft.insert(QStringLiteral("modelBackend"), model.value(QStringLiteral("backend")));
  draft.insert(QStringLiteral("modelPromptAudit"), plan.value(QStringLiteral("modelPromptAudit")));
  draft.insert(QStringLiteral("mcpDiscovery"), mcp_discovery);
  draft.insert(QStringLiteral("mcpObservation"), mcp_observation);
  if (plan.contains(QStringLiteral("modelToolPlan"))) {
    draft.insert(QStringLiteral("modelToolPlan"), plan.value(QStringLiteral("modelToolPlan")));
  }
  draft.insert(QStringLiteral("mcpAttached"), mcp_discovery.value(QStringLiteral("ready")).toBool());
  draft.insert(QStringLiteral("mcpToolCount"), mcp_discovery.value(QStringLiteral("toolCount")));
  draft.insert(QStringLiteral("mcpToolNames"), mcp_discovery.value(QStringLiteral("toolNames")));
  draft.insert(QStringLiteral("mcpToolPhase"), plan.value(QStringLiteral("mcpToolPhase")));
  draft.insert(QStringLiteral("mcpAllowedToolNames"), plan.value(QStringLiteral("mcpAllowedToolNames")));
  draft.insert(QStringLiteral("mcpAvailableToolCount"), plan.value(QStringLiteral("mcpAvailableToolCount")));
  draft.insert(QStringLiteral("mcpToolBudget"), plan.value(QStringLiteral("mcpToolBudget")));
  draft.insert(QStringLiteral("mcpEndpointKind"), mcp_discovery.value(QStringLiteral("endpointKind")));
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

QVariantMap AgentEditController::applySampleReviewToAll(bool confirmed) {
  const auto source_report = last_run_report_;
  const auto sample_review = source_report.value(QStringLiteral("sampleReview")).toMap();
  const auto remaining_asset_ids =
      sample_review.value(QStringLiteral("remainingAssetIds")).toStringList().isEmpty()
          ? source_report.value(QStringLiteral("remainingAssetIds")).toStringList()
          : sample_review.value(QStringLiteral("remainingAssetIds")).toStringList();
  QVariantMap report = result_map(false);
  report.insert(QStringLiteral("mode"), QStringLiteral("sample-review-apply-all"));
  report.insert(QStringLiteral("draftActive"), draftActive());
  report.insert(QStringLiteral("remainingAssetIds"), remaining_asset_ids);
  report.insert(QStringLiteral("remainingCount"), remaining_asset_ids.size());
  report.insert(QStringLiteral("sampleReview"), sample_review);
  report.insert(QStringLiteral("sampleOnlyDraft"), source_report.value(QStringLiteral("sampleOnlyDraft")).toBool());
  report.insert(QStringLiteral("plannedPatchesByAssetId"), source_report.value(QStringLiteral("plannedPatchesByAssetId")));
  report.insert(QStringLiteral("fullTargetAssetIds"), source_report.value(QStringLiteral("fullTargetAssetIds")));
  report.insert(QStringLiteral("fullTargetCount"), source_report.value(QStringLiteral("fullTargetCount")));
  report.insert(QStringLiteral("scope"), source_report.value(QStringLiteral("scope")));
  report.insert(QStringLiteral("recipePatch"), source_report.value(QStringLiteral("recipePatch")));
  report.insert(QStringLiteral("plannedRecipePatch"), source_report.value(QStringLiteral("plannedRecipePatch")));
  report.insert(QStringLiteral("batchClusters"), source_report.value(QStringLiteral("batchClusters")));
  report.insert(QStringLiteral("clusterPlanning"), source_report.value(QStringLiteral("clusterPlanning")));
  report.insert(QStringLiteral("representativeAssetIds"), source_report.value(QStringLiteral("representativeAssetIds")));
  report.insert(QStringLiteral("runId"), source_report.value(QStringLiteral("runId")));
  report.insert(QStringLiteral("sampleRevision"), source_report.value(QStringLiteral("sampleRevision")));
  report.insert(QStringLiteral("sampleRevisionFeedback"), source_report.value(QStringLiteral("sampleRevisionFeedback")));
  report.insert(QStringLiteral("feedbackMemoryDeferred"), source_report.value(QStringLiteral("feedbackMemoryDeferred")));
  if (!draftActive()) {
    record_failure_reason(&report, QStringLiteral("No active sample draft is available."));
    setStatus(QStringLiteral("Failed"));
    setErrorMessage(report.value(QStringLiteral("failureReason")).toString());
    setLastRunReport(report);
    return report;
  }
  if (!source_report.value(QStringLiteral("sampleOnlyDraft")).toBool() ||
      !sample_review.value(QStringLiteral("available")).toBool()) {
    record_failure_reason(&report, QStringLiteral("The active draft is not waiting on sample review."));
    setStatus(QStringLiteral("Failed"));
    setErrorMessage(report.value(QStringLiteral("failureReason")).toString());
    setLastRunReport(report);
    return report;
  }
  if (remaining_asset_ids.isEmpty()) {
    record_failure_reason(&report, QStringLiteral("There are no remaining photos to apply."));
    setStatus(QStringLiteral("Failed"));
    setErrorMessage(report.value(QStringLiteral("failureReason")).toString());
    setLastRunReport(report);
    return report;
  }
  if (!confirmed) {
    report.insert(QStringLiteral("confirmationRequired"), true);
    report.insert(QStringLiteral("ok"), false);
    report.insert(
        QStringLiteral("failureReason"),
        QStringLiteral("Confirm Apply to All before drafting the remaining photos."));
    report.insert(
        QStringLiteral("errors"),
        QStringList{report.value(QStringLiteral("failureReason")).toString()});
    setStatus(QStringLiteral("Awaiting Apply to All confirmation"));
    setErrorMessage(report.value(QStringLiteral("failureReason")).toString());
    setLastRunReport(report);
    return report;
  }

  auto patches_by_asset_id = patches_by_asset_id_from_variant_map(
      source_report.value(QStringLiteral("plannedPatchesByAssetId")).toMap());
  if (patches_by_asset_id.isEmpty()) {
    record_failure_reason(&report, QStringLiteral("The sample draft does not contain planned batch patches."));
    setStatus(QStringLiteral("Failed"));
    setErrorMessage(report.value(QStringLiteral("failureReason")).toString());
    setLastRunReport(report);
    return report;
  }

  QVariantMap metadata;
  metadata.insert(QStringLiteral("batchClusters"), source_report.value(QStringLiteral("batchClusters")));
  metadata.insert(QStringLiteral("clusterPlanning"), source_report.value(QStringLiteral("clusterPlanning")));
  metadata.insert(QStringLiteral("representativeAssetIds"), source_report.value(QStringLiteral("representativeAssetIds")));
  metadata.insert(QStringLiteral("plannedPatchesByAssetId"), source_report.value(QStringLiteral("plannedPatchesByAssetId")));
  metadata.insert(QStringLiteral("fullTargetAssetIds"), source_report.value(QStringLiteral("fullTargetAssetIds")));
  metadata.insert(QStringLiteral("explicitTargetAssetIds"), remaining_asset_ids);
  metadata.insert(QStringLiteral("remainingAssetIds"), QStringList{});
  metadata.insert(QStringLiteral("sampleOnlyContinuation"), true);
  metadata.insert(QStringLiteral("sampleDraftPolicy"), source_report.value(QStringLiteral("sampleDraftPolicy")));
  metadata.insert(QStringLiteral("sampleSourceRunId"), source_report.value(QStringLiteral("runId")));

  QVariantMap approved_feedback_report;
  const auto approved_feedback =
      source_report.value(QStringLiteral("sampleRevisionFeedback")).toString().trimmed();
  if (source_report.value(QStringLiteral("feedbackMemoryDeferred")).toBool() &&
      !approved_feedback.isEmpty()) {
    approved_feedback_report = recordStyleFeedback(
        approved_feedback,
        QVariantMap{
            {QStringLiteral("scope"), source_report.value(QStringLiteral("scope"))},
            {QStringLiteral("approvedByAction"), QStringLiteral("apply_to_all")},
            {QStringLiteral("sampleRunId"), source_report.value(QStringLiteral("runId"))},
            {QStringLiteral("sampleAssetIds"), sample_review.value(QStringLiteral("sampleAssetIds"))},
            {QStringLiteral("remainingAssetIds"), remaining_asset_ids},
        });
  }

  auto applied = applyDraftRecipePatchesForScope(
      source_report.value(QStringLiteral("scope"), QStringLiteral("selected-assets")).toString(),
      source_report.value(QStringLiteral("plannedRecipePatch"),
                          source_report.value(QStringLiteral("recipePatch")))
          .toMap(),
      patches_by_asset_id,
      metadata);
  applied.insert(QStringLiteral("mode"), QStringLiteral("sample-review-apply-all"));
  applied.insert(QStringLiteral("sampleOnlyContinuation"), applied.value(QStringLiteral("ok")).toBool());
  applied.insert(QStringLiteral("sampleSourceRunId"), metadata.value(QStringLiteral("sampleSourceRunId")));
  applied.insert(QStringLiteral("confirmationRequired"), false);
  if (!approved_feedback_report.isEmpty()) {
    applied.insert(QStringLiteral("sampleFeedbackApproved"), approved_feedback_report.value(QStringLiteral("ok")).toBool());
    applied.insert(QStringLiteral("feedbackMemoryDeferred"), false);
    applied.insert(QStringLiteral("approvedFeedbackMemory"), approved_feedback_report);
    applied.insert(QStringLiteral("approvedFeedbackMemoryNotePath"), approved_feedback_report.value(QStringLiteral("feedbackMemoryNotePath")));
    applied.insert(QStringLiteral("approvedFeedback"), approved_feedback);
    applied.insert(QStringLiteral("styleChangeSummary"), approved_feedback_report.value(QStringLiteral("styleChangeSummary")));
    applied.insert(QStringLiteral("styleChangeReason"), approved_feedback_report.value(QStringLiteral("styleChangeReason")));
    auto visible_events = applied.value(QStringLiteral("userVisibleMemoryEvents")).toList();
    visible_events.append(approved_feedback_report.value(QStringLiteral("userVisibleMemoryEvents")).toList());
    applied.insert(QStringLiteral("userVisibleMemoryEvents"), visible_events);
    if (!approved_feedback_report.value(QStringLiteral("ok")).toBool()) {
      auto warnings = applied.value(QStringLiteral("warnings")).toStringList();
      warnings.append(approved_feedback_report.value(QStringLiteral("errors")).toStringList());
      applied.insert(QStringLiteral("warnings"), warnings);
    }
  }
  setLastRunReport(applied);
  return applied;
}

QVariantMap AgentEditController::reviseSampleReview(const QString& feedback) {
  const auto trimmed_feedback = feedback.trimmed();
  const auto source_report = last_run_report_;
  const auto sample_review = source_report.value(QStringLiteral("sampleReview")).toMap();
  const auto remaining_asset_ids =
      sample_review.value(QStringLiteral("remainingAssetIds")).toStringList().isEmpty()
          ? source_report.value(QStringLiteral("remainingAssetIds")).toStringList()
          : sample_review.value(QStringLiteral("remainingAssetIds")).toStringList();
  QVariantMap report = result_map(false);
  report.insert(QStringLiteral("mode"), QStringLiteral("sample-review-revision"));
  report.insert(QStringLiteral("draftActive"), draftActive());
  report.insert(QStringLiteral("sampleReview"), sample_review);
  report.insert(QStringLiteral("remainingAssetIds"), remaining_asset_ids);
  report.insert(QStringLiteral("feedback"), trimmed_feedback);
  if (trimmed_feedback.isEmpty()) {
    record_failure_reason(&report, QStringLiteral("Describe what should change in the samples."));
    setStatus(QStringLiteral("Waiting for sample feedback"));
    setErrorMessage(report.value(QStringLiteral("failureReason")).toString());
    setLastRunReport(report);
    return report;
  }
  if (!draftActive() ||
      !source_report.value(QStringLiteral("sampleOnlyDraft")).toBool() ||
      !sample_review.value(QStringLiteral("available")).toBool() ||
      remaining_asset_ids.isEmpty()) {
    record_failure_reason(&report, QStringLiteral("No active sample review is available to revise."));
    setStatus(QStringLiteral("Failed"));
    setErrorMessage(report.value(QStringLiteral("failureReason")).toString());
    setLastRunReport(report);
    return report;
  }

  const auto source_run_id = source_report.value(QStringLiteral("runId")).toString();
  const auto source_scope =
      source_report.value(QStringLiteral("scope"), QStringLiteral("selected-assets")).toString();
  const auto base_instructions = user_instructions_.trimmed();
  const auto revised_instructions =
      base_instructions.isEmpty()
          ? QStringLiteral("Sample review feedback:\n%1").arg(trimmed_feedback)
          : QStringLiteral("%1\n\nSample review feedback:\n%2").arg(base_instructions, trimmed_feedback);
  const auto rollback_report = rollbackDraft();
  if (!rollback_report.value(QStringLiteral("ok")).toBool()) {
    auto failed = result_map(
        false,
        rollback_report.value(QStringLiteral("errors")).toStringList());
    failed.insert(QStringLiteral("mode"), QStringLiteral("sample-review-revision"));
    failed.insert(QStringLiteral("rollbackReport"), rollback_report);
    setLastRunReport(failed);
    return failed;
  }

  auto revised = runDraftForScope(source_scope, revised_instructions);
  revised.insert(QStringLiteral("sampleRevision"), revised.value(QStringLiteral("ok")).toBool());
  revised.insert(QStringLiteral("sampleRevisionFeedback"), trimmed_feedback);
  revised.insert(QStringLiteral("revisedFromRunId"), source_run_id);
  revised.insert(QStringLiteral("rollbackBeforeRevision"), rollback_report);
  revised.insert(QStringLiteral("feedbackMemoryDeferred"), true);
  revised.insert(
      QStringLiteral("feedbackMemoryPolicy"),
      QStringLiteral("Sample feedback is used for immediate revision and saved to durable style memory only through an explicit feedback action."));
  setLastRunReport(revised);
  return revised;
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
  const auto scoped_assets = scoped_asset_maps(catalog_model_, scope);
  const auto sample_only_asset_ids =
      batch_plan_metadata.value(QStringLiteral("sampleOnlyAssetIds")).toStringList();
  const auto explicit_target_asset_ids =
      batch_plan_metadata.value(QStringLiteral("explicitTargetAssetIds")).toStringList();
  const auto selected_assets = filter_asset_maps_by_ids(
      scoped_assets,
      !explicit_target_asset_ids.isEmpty() ? explicit_target_asset_ids : sample_only_asset_ids);
  const auto report_scope = report_scope_for_assets(scope, scoped_assets);
  const auto scope_context =
      active_project_scope_context(catalog_model_, scope, selected_assets);
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
  QVariantList edit_loop_reports;
  QStringList asset_ids;
  QVariantMap first_final_patch;
  QVariantMap first_risk_assessment;
  auto report_warnings = patch_validation_warnings;
  const auto base_mode = normalized_recipe_base_mode(recipe_base_mode_);
  const auto manual_edit_policy = manual_edit_policy_for_base_mode(base_mode);
  auto batch_clusters = batch_plan_metadata.value(QStringLiteral("batchClusters")).toList();
  const auto asset_cluster_assignments =
      batch_plan_metadata.value(QStringLiteral("assetClusterAssignments")).toMap();
  QVariantMap resume_clean_reports_by_id;
  for (const auto& report_value :
       batch_plan_metadata.value(QStringLiteral("resumeCleanAssetReports")).toList()) {
    const auto report = report_value.toMap();
    const auto asset_id = report.value(QStringLiteral("assetId")).toString();
    if (!asset_id.isEmpty()) {
      resume_clean_reports_by_id.insert(asset_id, report);
    }
  }
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
    const auto resume_clean_report = resume_clean_reports_by_id.value(asset_id).toMap();
    if (!resume_clean_report.isEmpty() &&
        resume_clean_report.value(QStringLiteral("originalRecipeJson")).toString() == original_recipe_json) {
      const auto resumed_patch = resume_clean_report.value(QStringLiteral("finalRecipePatch")).toMap();
      if (!resumed_patch.isEmpty()) {
        auto resumed_state = append_named_recipe_snapshot(
            original_state,
            QStringLiteral("Agent draft %1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)),
            QStringLiteral("agent-%1").arg(draft_run_id_.left(8)));
        resumed_state = agent_base_state_for_mode(original_state, resumed_state, base_mode);
        tag_latest_agent_snapshot(
            &resumed_state,
            draft_run_id_,
            selected_model_id_,
            selected_style_id_,
            QDateTime::currentDateTimeUtc());
        apply_agent_recipe_patch(&resumed_state, resumed_patch);
        annotate_latest_agent_snapshot_diff(
            &resumed_state,
            resumed_patch,
            resume_clean_report.value(QStringLiteral("recipeDiff")).toMap());
        const auto resumed_recipe_json = serialize_edit_recipe_json(resumed_state);
        const auto resumed_recipe_validation = validate_edit_recipe_json(resumed_recipe_json);
        if (resumed_recipe_validation.valid) {
          auto resumed_asset_report = resume_clean_report;
          resumed_asset_report.insert(QStringLiteral("status"), QStringLiteral("prepared"));
          resumed_asset_report.insert(QStringLiteral("resumedFromRunId"), batch_plan_metadata.value(QStringLiteral("resumeSourceRunId")));
          resumed_asset_report.insert(QStringLiteral("validationSkippedByResume"), true);
          asset_reports.append(resumed_asset_report);
          asset_ids.append(asset_id);
          original_recipes.insert(asset_id, original_recipe_json);
          next_recipes.insert(asset_id, resumed_recipe_json);
          report_warnings.append(resumed_asset_report.value(QStringLiteral("warnings")).toStringList());
          if (first_final_patch.isEmpty()) {
            first_final_patch = resumed_patch;
            first_risk_assessment = resumed_asset_report.value(QStringLiteral("riskAssessment")).toMap();
          }
          append_tool_call(
              &tool_calls,
              QStringLiteral("reuse_resumable_asset_validation"),
              QStringLiteral("ok"),
              QVariantMap{
                  {QStringLiteral("assetId"), asset_id},
                  {QStringLiteral("sourceRunId"), batch_plan_metadata.value(QStringLiteral("resumeSourceRunId"))},
              });
          continue;
        }
      }
    }
    auto final_patch = requested_patch;
    auto final_state = original_state;
    QString next_recipe_json;
    QVariantMap image_validation;
    QVariantList validation_iterations;
    QVariantList edit_loop_attempts;
    QSet<QString> attempted_patch_hashes;
    auto revision_count = 0;
    auto model_repair_count = 0;
    auto stopped_at_iteration_limit = false;
    auto repeated_patch_stop = false;
    auto no_score_improvement_stop = false;
    auto edit_loop_needs_user_review = false;
    auto best_attempt_index = -1;
    auto final_quality_score = 0.0;
    auto best_quality_score = -1.0;
    QString edit_loop_stop_reason;
    QVariantList model_repair_reports;
    const auto repair_context = batch_plan_metadata.value(QStringLiteral("modelRepairContext")).toMap();
    const auto repair_model =
        model_metadata_by_id(available_models_, repair_context.value(QStringLiteral("modelId")).toString());
    const auto model_proof_critique_available =
        repair_model.value(QStringLiteral("backend")).toString() == QStringLiteral("ollama") &&
        repair_model.value(QStringLiteral("vision")).toBool();
    for (auto iteration = 0; iteration <= kMaxValidationRevisionIterations; ++iteration) {
      if (stop_requested()) {
        edit_loop_stop_reason = QStringLiteral("cancelled");
        break;
      }
      const auto patch_hash = stable_variant_hash(final_patch);
      if (attempted_patch_hashes.contains(patch_hash)) {
        repeated_patch_stop = true;
        edit_loop_stop_reason = QStringLiteral("repeated_patch");
        break;
      }
      attempted_patch_hashes.insert(patch_hash);
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
        edit_loop_stop_reason = QStringLiteral("invalid_recipe_json");
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
      const auto quality_score = edit_loop_quality_score(image_validation);
      const auto previous_best_quality_score = best_quality_score;
      final_quality_score = quality_score;
      if (quality_score > best_quality_score) {
        best_quality_score = quality_score;
        best_attempt_index = iteration;
      }
      QVariantMap model_proof_critique;
      if (!iteration_warnings.isEmpty() && !repair_context.isEmpty() && model_proof_critique_available) {
        auto critique_asset = asset;
        const auto artifacts = image_validation.value(QStringLiteral("artifacts")).toMap();
        const auto after_proof_path = artifacts.value(QStringLiteral("afterProofPath")).toString();
        if (!after_proof_path.trimmed().isEmpty()) {
          critique_asset.insert(QStringLiteral("proofImagePath"), after_proof_path);
          critique_asset.insert(QStringLiteral("afterProofPath"), after_proof_path);
        }
        AgentLocalModelProofCritiqueRequest critique_request;
        critique_request.model_id = repair_context.value(QStringLiteral("modelId")).toString();
        critique_request.style = style;
        critique_request.asset = critique_asset;
        critique_request.user_instructions =
            repair_context.value(QStringLiteral("userInstructions")).toString();
        critique_request.validation_report = image_validation;
        critique_request.previous_attempts = edit_loop_attempts;
        const auto critique = critique_agent_rendered_proof(critique_request);
        model_proof_critique.insert(QStringLiteral("mode"), QStringLiteral("model-proof-critique"));
        model_proof_critique.insert(QStringLiteral("ok"), critique.ok);
        model_proof_critique.insert(QStringLiteral("modelId"), critique.model_id);
        model_proof_critique.insert(QStringLiteral("pass"), critique.pass);
        model_proof_critique.insert(QStringLiteral("summary"), critique.summary);
        model_proof_critique.insert(QStringLiteral("issues"), critique.issues);
        model_proof_critique.insert(QStringLiteral("repairHints"), critique.repair_hints);
        model_proof_critique.insert(QStringLiteral("needsUserReview"), critique.needs_user_review);
        model_proof_critique.insert(QStringLiteral("tasteConfidence"), critique.taste_confidence);
        model_proof_critique.insert(QStringLiteral("warnings"), critique.warnings);
        model_proof_critique.insert(QStringLiteral("promptAudit"), critique.prompt_audit);
        if (!critique.ok) {
          model_proof_critique.insert(QStringLiteral("errors"), QStringList{critique.error_message});
        }
        image_validation.insert(QStringLiteral("modelProofCritique"), model_proof_critique);
        append_tool_call(
            &tool_calls,
            QStringLiteral("model_evaluate_rendered_proof"),
            critique.ok ? QStringLiteral("ok") : QStringLiteral("failed"),
            QVariantMap{
                {QStringLiteral("assetId"), asset_id},
                {QStringLiteral("iteration"), iteration},
                {QStringLiteral("modelId"), critique.model_id},
                {QStringLiteral("pass"), critique.pass},
                {QStringLiteral("needsUserReview"), critique.needs_user_review},
                {QStringLiteral("issueCount"), critique.issues.size()},
                {QStringLiteral("repairHintCount"), critique.repair_hints.size()},
                {QStringLiteral("error"), critique.error_message},
            });
      }
      const auto attempt_needs_user_review =
          image_validation.value(QStringLiteral("needsUserReview")).toBool() ||
          image_validation.value(QStringLiteral("qualityScoreReport"))
              .toMap()
              .value(QStringLiteral("needsUserReview"))
              .toBool() ||
          image_validation.value(QStringLiteral("styleMatch"))
              .toMap()
              .value(QStringLiteral("needsReview"))
              .toBool() ||
          image_validation.value(QStringLiteral("modelProofCritique"))
              .toMap()
              .value(QStringLiteral("needsUserReview"))
              .toBool();
      edit_loop_needs_user_review = edit_loop_needs_user_review || attempt_needs_user_review;
      auto attempt_stop_reason = QStringLiteral("repair_requested");
      if (!image_validation.value(QStringLiteral("ok")).toBool()) {
        attempt_stop_reason = QStringLiteral("validation_failed");
      } else if (iteration_warnings.isEmpty()) {
        attempt_stop_reason = QStringLiteral("validated_clean");
      } else if (iteration == kMaxValidationRevisionIterations) {
        attempt_stop_reason = QStringLiteral("attempt_limit");
      } else if (iteration > 0 &&
                 previous_best_quality_score >= 0.0 &&
                 quality_score < previous_best_quality_score + kMinEditLoopScoreImprovement) {
        attempt_stop_reason = QStringLiteral("no_score_improvement");
      }
      validation_iterations.append(QVariantMap{
          {QStringLiteral("iteration"), iteration},
          {QStringLiteral("ok"), image_validation.value(QStringLiteral("ok"))},
          {QStringLiteral("warningCount"), iteration_warnings.size()},
          {QStringLiteral("warnings"), iteration_warnings},
          {QStringLiteral("recipePatch"), final_patch},
          {QStringLiteral("patchHash"), patch_hash},
          {QStringLiteral("qualityScore"), quality_score},
          {QStringLiteral("qualityScorePercent"), static_cast<int>(std::round(quality_score * 100.0))},
          {QStringLiteral("needsUserReview"), attempt_needs_user_review},
          {QStringLiteral("stopReasonCandidate"), attempt_stop_reason},
      });
      edit_loop_attempts.append(QVariantMap{
          {QStringLiteral("attemptIndex"), iteration},
          {QStringLiteral("patchHash"), patch_hash},
          {QStringLiteral("recipePatch"), final_patch},
          {QStringLiteral("proofPath"),
           image_validation.value(QStringLiteral("artifacts")).toMap().value(QStringLiteral("afterProofPath"))},
          {QStringLiteral("validationReport"), image_validation},
          {QStringLiteral("styleMatch"), image_validation.value(QStringLiteral("styleMatch"))},
          {QStringLiteral("modelProofCritique"), model_proof_critique},
          {QStringLiteral("qualityScore"), quality_score},
          {QStringLiteral("qualityScorePercent"), static_cast<int>(std::round(quality_score * 100.0))},
          {QStringLiteral("warnings"), iteration_warnings},
          {QStringLiteral("repairSource"), iteration == 0 ? QStringLiteral("initial")
                                                          : QStringLiteral("repair")},
          {QStringLiteral("needsUserReview"), attempt_needs_user_review},
          {QStringLiteral("stopReasonCandidate"), attempt_stop_reason},
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
              {QStringLiteral("qualityScore"), quality_score},
              {QStringLiteral("patchHash"), patch_hash},
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
          iteration == kMaxValidationRevisionIterations ||
          attempt_stop_reason == QStringLiteral("no_score_improvement")) {
        stopped_at_iteration_limit = !iteration_warnings.isEmpty() &&
                                    iteration == kMaxValidationRevisionIterations;
        no_score_improvement_stop =
            attempt_stop_reason == QStringLiteral("no_score_improvement");
        edit_loop_stop_reason = attempt_stop_reason;
        break;
      }
      auto revised_patch = QVariantMap{};
      auto model_repair_used_this_iteration = false;
      if (model_repair_count < kMaxModelRepairTurnsPerAsset && !repair_context.isEmpty()) {
        auto repair_report = model_repair_recipe_patch_from_validation(
            repair_context,
            asset,
            original_recipe_json,
            final_patch,
            image_validation,
            style,
            edit_loop_attempts);
        repair_report.insert(QStringLiteral("assetId"), asset_id);
        repair_report.insert(QStringLiteral("iteration"), iteration);
        model_repair_reports.append(repair_report);
        append_tool_call(
            &tool_calls,
            QStringLiteral("model_repair_recipe_patch_from_validation"),
            repair_report.value(QStringLiteral("ok")).toBool() ? QStringLiteral("ok") : QStringLiteral("failed"),
            QVariantMap{
                {QStringLiteral("assetId"), asset_id},
                {QStringLiteral("iteration"), iteration},
                {QStringLiteral("warningCount"), iteration_warnings.size()},
                {QStringLiteral("modelId"), repair_context.value(QStringLiteral("modelId"))},
                {QStringLiteral("decision"), repair_report.value(QStringLiteral("decision"))},
                {QStringLiteral("error"), repair_report.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("; "))},
            });
        if (repair_report.value(QStringLiteral("ok")).toBool()) {
          revised_patch = repair_report.value(QStringLiteral("recipePatch")).toMap();
          ++model_repair_count;
          model_repair_used_this_iteration = true;
        }
      }
      if (revised_patch.isEmpty()) {
        revised_patch = revise_recipe_patch_from_validation(final_patch, image_validation);
      }
      if (revised_patch == final_patch) {
        edit_loop_stop_reason = QStringLiteral("repair_produced_same_patch");
        break;
      }
      final_patch = revised_patch;
      ++revision_count;
      append_tool_call(
          &tool_calls,
          model_repair_used_this_iteration
              ? QStringLiteral("apply_model_repaired_recipe_patch")
              : QStringLiteral("revise_recipe_patch_from_validation"),
          QStringLiteral("ok"),
          QVariantMap{
              {QStringLiteral("assetId"), asset_id},
              {QStringLiteral("iteration"), iteration},
              {QStringLiteral("warningCount"), iteration_warnings.size()},
              {QStringLiteral("modelRepairCount"), model_repair_count},
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
    if (edit_loop_stop_reason.isEmpty()) {
      edit_loop_stop_reason =
          image_validation.value(QStringLiteral("warnings")).toStringList().isEmpty()
              ? QStringLiteral("validated_clean")
              : QStringLiteral("validated_with_warnings");
    }

    report_warnings.append(image_validation.value(QStringLiteral("warnings")).toStringList());
    if (first_final_patch.isEmpty()) {
      first_final_patch = final_patch;
      first_risk_assessment = agent_risk_assessment(
          original_state,
          image_validation.value(QStringLiteral("warnings")).toStringList(),
          final_patch,
          scope_context);
    }

    QVariantMap asset_report;
    asset_report.insert(QStringLiteral("assetId"), asset_id);
    asset_report.insert(QStringLiteral("originalRecipeJson"), original_recipe_json);
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
            final_patch,
            active_project_scope_context(catalog_model_, scope, QVariantList{asset})));
    asset_report.insert(QStringLiteral("revisionCount"), revision_count);
    asset_report.insert(QStringLiteral("modelRepairCount"), model_repair_count);
    asset_report.insert(QStringLiteral("modelRepairReports"), model_repair_reports);
    asset_report.insert(QStringLiteral("qualityScore"), final_quality_score);
    asset_report.insert(
        QStringLiteral("qualityScorePercent"),
        static_cast<int>(std::round(final_quality_score * 100.0)));
    asset_report.insert(QStringLiteral("needsUserReview"), edit_loop_needs_user_review);
    asset_report.insert(QStringLiteral("validationIterationLimit"), kMaxValidationRevisionIterations);
    asset_report.insert(QStringLiteral("validationIterationLimitReached"), stopped_at_iteration_limit);
    asset_report.insert(
        QStringLiteral("repairBudget"),
        [&]() {
          auto budget = edit_loop_budget_report();
          budget.insert(QStringLiteral("revisionCount"), revision_count);
          budget.insert(QStringLiteral("modelRepairCount"), model_repair_count);
          budget.insert(QStringLiteral("validationIterationLimitReached"), stopped_at_iteration_limit);
          budget.insert(QStringLiteral("repeatedPatchStop"), repeated_patch_stop);
          budget.insert(QStringLiteral("noScoreImprovementStop"), no_score_improvement_stop);
          return budget;
        }());
    asset_report.insert(QStringLiteral("validationIterations"), validation_iterations);
    const QVariantMap edit_loop_report{
        {QStringLiteral("version"), QStringLiteral("agent-edit-loop-v1")},
        {QStringLiteral("assetId"), asset_id},
        {QStringLiteral("maxAttempts"), kMaxValidationRevisionIterations + 1},
        {QStringLiteral("maxModelRepairTurns"), kMaxModelRepairTurnsPerAsset},
        {QStringLiteral("minScoreImprovement"), kMinEditLoopScoreImprovement},
        {QStringLiteral("attempts"), edit_loop_attempts},
        {QStringLiteral("attemptCount"), edit_loop_attempts.size()},
        {QStringLiteral("bestAttemptIndex"), best_attempt_index},
        {QStringLiteral("finalStopReason"), edit_loop_stop_reason},
        {QStringLiteral("finalScore"), final_quality_score},
        {QStringLiteral("finalScorePercent"),
         static_cast<int>(std::round(final_quality_score * 100.0))},
        {QStringLiteral("acceptedForDraft"), true},
        {QStringLiteral("needsUserReview"), edit_loop_needs_user_review},
        {QStringLiteral("repeatedPatchStop"), repeated_patch_stop},
        {QStringLiteral("noScoreImprovementStop"), no_score_improvement_stop},
    };
    asset_report.insert(QStringLiteral("editLoop"), edit_loop_report);
    edit_loop_reports.append(edit_loop_report);
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
    report.insert(QStringLiteral("requestedRecipePatch"), patch);
    report.insert(
        QStringLiteral("resumePlan"),
        batch_resume_plan(
            draft_run_id_,
            report_scope,
            patch,
            asset_reports,
            batch_clusters,
            batch_plan_metadata.value(QStringLiteral("clusterPlanning")),
            batch_plan_metadata.value(QStringLiteral("representativeAssetIds"))));
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
    if (batch_plan_metadata.contains(QStringLiteral("resumeSourceRunId"))) {
      report.insert(QStringLiteral("resumed"), true);
      report.insert(QStringLiteral("resumedFromRunId"), batch_plan_metadata.value(QStringLiteral("resumeSourceRunId")));
    }
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
    report.insert(QStringLiteral("requestedRecipePatch"), patch);
    report.insert(
        QStringLiteral("resumePlan"),
        batch_resume_plan(
            draft_run_id_,
            report_scope,
            patch,
            asset_reports,
            batch_clusters,
            batch_plan_metadata.value(QStringLiteral("clusterPlanning")),
            batch_plan_metadata.value(QStringLiteral("representativeAssetIds"))));
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
    if (batch_plan_metadata.contains(QStringLiteral("resumeSourceRunId"))) {
      report.insert(QStringLiteral("resumed"), true);
      report.insert(QStringLiteral("resumedFromRunId"), batch_plan_metadata.value(QStringLiteral("resumeSourceRunId")));
    }
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
  if (batch_plan_metadata.contains(QStringLiteral("resumeSourceRunId"))) {
    report.insert(QStringLiteral("resumed"), true);
    report.insert(QStringLiteral("resumedFromRunId"), batch_plan_metadata.value(QStringLiteral("resumeSourceRunId")));
  }
  report.insert(QStringLiteral("editGoals"), agent_edit_goals(style, user_instructions_, first_final_patch));
  report.insert(QStringLiteral("riskAssessment"), first_risk_assessment);
  report.insert(QStringLiteral("requestedRecipePatch"), patch);
  report.insert(QStringLiteral("editLoopVersion"), QStringLiteral("agent-edit-loop-v1"));
  report.insert(
      QStringLiteral("editLoopSummary"),
      QVariantMap{
          {QStringLiteral("assetCount"), edit_loop_reports.size()},
          {QStringLiteral("maxAttempts"), kMaxValidationRevisionIterations + 1},
          {QStringLiteral("maxModelRepairTurns"), kMaxModelRepairTurnsPerAsset},
          {QStringLiteral("minScoreImprovement"), kMinEditLoopScoreImprovement},
      });
  report.insert(QStringLiteral("editLoops"), edit_loop_reports);
  report.insert(QStringLiteral("clusterPlanning"), batch_plan_metadata.value(QStringLiteral("clusterPlanning")));
  report.insert(QStringLiteral("batchClusters"), batch_clusters);
  report.insert(QStringLiteral("clusterCount"), batch_clusters.size());
  report.insert(QStringLiteral("representativeAssetIds"), batch_plan_metadata.value(QStringLiteral("representativeAssetIds")));
  const auto full_target_asset_ids =
      batch_plan_metadata.value(QStringLiteral("fullTargetAssetIds")).toStringList();
  const auto remaining_asset_ids =
      batch_plan_metadata.contains(QStringLiteral("remainingAssetIds"))
          ? batch_plan_metadata.value(QStringLiteral("remainingAssetIds")).toStringList()
          : ordered_difference(full_target_asset_ids, asset_ids);
  report.insert(
      QStringLiteral("fullTargetAssetIds"),
      full_target_asset_ids.isEmpty() ? asset_ids : full_target_asset_ids);
  report.insert(
      QStringLiteral("fullTargetCount"),
      full_target_asset_ids.isEmpty() ? asset_ids.size() : full_target_asset_ids.size());
  report.insert(QStringLiteral("remainingAssetIds"), remaining_asset_ids);
  report.insert(QStringLiteral("remainingCount"), remaining_asset_ids.size());
  report.insert(QStringLiteral("sampleOnlyDraft"), !sample_only_asset_ids.isEmpty());
  report.insert(
      QStringLiteral("sampleOnlyContinuation"),
      batch_plan_metadata.value(QStringLiteral("sampleOnlyContinuation")).toBool());
  report.insert(QStringLiteral("sampleDraftPolicy"), batch_plan_metadata.value(QStringLiteral("sampleDraftPolicy")));
  report.insert(QStringLiteral("plannedPatchesByAssetId"), batch_plan_metadata.value(QStringLiteral("plannedPatchesByAssetId")));
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
      QStringLiteral("sampleReview"),
      sample_review_report(
          asset_reports,
          batch_plan_metadata.value(QStringLiteral("representativeAssetIds")).toStringList(),
          full_target_asset_ids.isEmpty() ? asset_ids : full_target_asset_ids,
          remaining_asset_ids,
          batch_plan_metadata.value(QStringLiteral("clusterPlanning")).toList(),
          report.value(QStringLiteral("planSummary")).toString()));
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

QVariantMap AgentEditController::resumeLastFailedBatchDraft() {
  const auto resume_plan = last_run_report_.value(QStringLiteral("resumePlan")).toMap();
  if (resume_plan.isEmpty() || !resume_plan.value(QStringLiteral("available")).toBool()) {
    auto report = result_map(false, {QStringLiteral("No resumable failed batch draft is available.")});
    report.insert(QStringLiteral("mode"), QStringLiteral("resume-draft"));
    setStatus(QStringLiteral("Failed"));
    setErrorMessage(report.value(QStringLiteral("failureReason")).toString());
    setLastRunReport(report);
    return report;
  }
  if (draftActive()) {
    auto report = result_map(false, {QStringLiteral("Rollback or commit the active draft before resuming another failed batch.")});
    report.insert(QStringLiteral("mode"), QStringLiteral("resume-draft"));
    setStatus(QStringLiteral("Failed"));
    setErrorMessage(report.value(QStringLiteral("failureReason")).toString());
    setLastRunReport(report);
    return report;
  }

  QHash<QString, QVariantMap> patches_by_asset_id;
  const auto per_asset_patches = resume_plan.value(QStringLiteral("perAssetPatches")).toMap();
  for (auto iterator = per_asset_patches.cbegin(); iterator != per_asset_patches.cend(); ++iterator) {
    const auto patch = iterator.value().toMap();
    if (!iterator.key().isEmpty() && !patch.isEmpty()) {
      patches_by_asset_id.insert(iterator.key(), patch);
    }
  }

  QVariantMap metadata;
  metadata.insert(QStringLiteral("batchClusters"), resume_plan.value(QStringLiteral("batchClusters")));
  metadata.insert(QStringLiteral("clusterPlanning"), resume_plan.value(QStringLiteral("clusterPlanning")));
  metadata.insert(QStringLiteral("representativeAssetIds"), resume_plan.value(QStringLiteral("representativeAssetIds")));
  metadata.insert(QStringLiteral("resumeCleanAssetReports"), resume_plan.value(QStringLiteral("cleanAssetReports")));
  metadata.insert(QStringLiteral("resumeSourceRunId"), resume_plan.value(QStringLiteral("sourceRunId")));
  auto report = applyDraftRecipePatchesForScope(
      resume_plan.value(QStringLiteral("scope"), QStringLiteral("selected-assets")).toString(),
      resume_plan.value(QStringLiteral("defaultRecipePatch")).toMap(),
      patches_by_asset_id,
      metadata);
  report.insert(QStringLiteral("resumed"), report.value(QStringLiteral("ok")).toBool());
  report.insert(QStringLiteral("resumedFromRunId"), resume_plan.value(QStringLiteral("sourceRunId")));
  setLastRunReport(report);
  return report;
}

QVariantMap AgentEditController::acceptReviewIssue(const QString& asset_id) {
  const auto trimmed_asset_id = asset_id.trimmed();
  if (trimmed_asset_id.isEmpty()) {
    auto report = result_map(false, {QStringLiteral("Review issue asset id is required.")});
    report.insert(QStringLiteral("mode"), QStringLiteral("review-issue-accept"));
    setLastRunReport(report);
    return report;
  }

  auto updated_report = last_run_report_;
  auto asset_reports = updated_report.value(QStringLiteral("assetReports")).toList();
  auto found = false;
  for (auto index = 0; index < asset_reports.size(); ++index) {
    auto asset_report = asset_reports.at(index).toMap();
    if (asset_report.value(QStringLiteral("assetId")).toString() != trimmed_asset_id) {
      continue;
    }
    found = true;
    asset_report.insert(QStringLiteral("reviewIssueResolved"), true);
    asset_report.insert(QStringLiteral("reviewIssueResolution"), QStringLiteral("accepted_anyway"));
    asset_report.insert(QStringLiteral("reviewIssueResolvedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    asset_reports[index] = asset_report;
    break;
  }
  if (!found) {
    auto report = result_map(false, {QStringLiteral("No review issue asset was found for %1.").arg(trimmed_asset_id)});
    report.insert(QStringLiteral("mode"), QStringLiteral("review-issue-accept"));
    report.insert(QStringLiteral("assetId"), trimmed_asset_id);
    setLastRunReport(report);
    return report;
  }

  updated_report.insert(QStringLiteral("assetReports"), asset_reports);
  const auto summary = batch_summary(
      updated_report.value(QStringLiteral("scope")).toString(),
      updated_report.value(QStringLiteral("assetIds")).toStringList(),
      asset_reports,
      draftActive(),
      draftActive());
  updated_report.insert(QStringLiteral("batchSummary"), summary);
  QVariantMap action_report = result_map(true);
  action_report.insert(QStringLiteral("mode"), QStringLiteral("review-issue-accept"));
  action_report.insert(QStringLiteral("assetId"), trimmed_asset_id);
  action_report.insert(QStringLiteral("resolution"), QStringLiteral("accepted_anyway"));
  action_report.insert(QStringLiteral("batchSummary"), summary);
  updated_report.insert(QStringLiteral("lastReviewIssueAction"), action_report);
  setLastRunReport(updated_report);
  return action_report;
}

QVariantMap AgentEditController::fixReviewIssue(const QString& asset_id) {
  const auto trimmed_asset_id = asset_id.trimmed();
  if (trimmed_asset_id.isEmpty()) {
    auto report = result_map(false, {QStringLiteral("Review issue asset id is required.")});
    report.insert(QStringLiteral("mode"), QStringLiteral("review-issue-fix"));
    setLastRunReport(report);
    return report;
  }
  if (!draftActive()) {
    auto report = result_map(false, {QStringLiteral("No active draft is available to repair for %1.").arg(trimmed_asset_id)});
    report.insert(QStringLiteral("mode"), QStringLiteral("review-issue-fix"));
    report.insert(QStringLiteral("assetId"), trimmed_asset_id);
    setLastRunReport(report);
    return report;
  }

  const auto source_report = last_run_report_;
  QVariantMap source_asset_report;
  const auto source_asset_reports = source_report.value(QStringLiteral("assetReports")).toList();
  for (const auto& source_asset_report_value : source_asset_reports) {
    const auto candidate = source_asset_report_value.toMap();
    if (candidate.value(QStringLiteral("assetId")).toString() == trimmed_asset_id) {
      source_asset_report = candidate;
      break;
    }
  }
  if (source_asset_report.isEmpty()) {
    auto report = result_map(false, {QStringLiteral("No review issue asset was found for %1.").arg(trimmed_asset_id)});
    report.insert(QStringLiteral("mode"), QStringLiteral("review-issue-fix"));
    report.insert(QStringLiteral("assetId"), trimmed_asset_id);
    setLastRunReport(report);
    return report;
  }

  auto repair_patch = source_asset_report.value(QStringLiteral("finalRecipePatch")).toMap();
  if (repair_patch.isEmpty()) {
    repair_patch = source_report.value(QStringLiteral("recipePatch")).toMap();
  }
  if (repair_patch.isEmpty()) {
    auto report = result_map(false, {QStringLiteral("No repair patch is available for %1.").arg(trimmed_asset_id)});
    report.insert(QStringLiteral("mode"), QStringLiteral("review-issue-fix"));
    report.insert(QStringLiteral("assetId"), trimmed_asset_id);
    setLastRunReport(report);
    return report;
  }

  const auto validation_report = source_asset_report.value(QStringLiteral("validationReport")).toMap();
  auto revised_patch = revise_recipe_patch_from_validation(repair_patch, validation_report);
  if (QJsonDocument::fromVariant(revised_patch).toJson(QJsonDocument::Compact) ==
      QJsonDocument::fromVariant(repair_patch).toJson(QJsonDocument::Compact)) {
    adjust_patch_number(
        &revised_patch,
        QStringLiteral("globalTone"),
        QStringLiteral("exposure"),
        -0.05,
        -5.0,
        5.0);
  }

  QVariantMap metadata;
  metadata.insert(QStringLiteral("batchClusters"), source_report.value(QStringLiteral("batchClusters")));
  metadata.insert(QStringLiteral("clusterPlanning"), source_report.value(QStringLiteral("clusterPlanning")));
  metadata.insert(QStringLiteral("representativeAssetIds"), source_report.value(QStringLiteral("representativeAssetIds")));
  metadata.insert(QStringLiteral("fullTargetAssetIds"), source_report.value(QStringLiteral("fullTargetAssetIds")));
  metadata.insert(QStringLiteral("explicitTargetAssetIds"), QStringList{trimmed_asset_id});
  metadata.insert(QStringLiteral("sampleOnlyContinuation"), source_report.value(QStringLiteral("sampleOnlyContinuation")).toBool());
  metadata.insert(QStringLiteral("sampleDraftPolicy"), source_report.value(QStringLiteral("sampleDraftPolicy")));

  QHash<QString, QVariantMap> patches_by_asset_id;
  patches_by_asset_id.insert(trimmed_asset_id, revised_patch);
  auto fixed_report = applyDraftRecipePatchesForScope(
      source_report.value(QStringLiteral("scope"), QStringLiteral("selected-assets")).toString(),
      revised_patch,
      patches_by_asset_id,
      metadata);
  fixed_report.insert(QStringLiteral("mode"), QStringLiteral("review-issue-fix"));
  fixed_report.insert(QStringLiteral("assetId"), trimmed_asset_id);
  fixed_report.insert(QStringLiteral("repairedRecipePatch"), revised_patch);
  if (!fixed_report.value(QStringLiteral("ok")).toBool()) {
    setLastRunReport(fixed_report);
    return fixed_report;
  }

  const auto fixed_asset_reports = fixed_report.value(QStringLiteral("assetReports")).toList();
  if (fixed_asset_reports.isEmpty()) {
    fixed_report.insert(QStringLiteral("ok"), false);
    fixed_report.insert(
        QStringLiteral("errors"),
        QStringList{QStringLiteral("The repair run did not return an asset report for %1.").arg(trimmed_asset_id)});
    setLastRunReport(fixed_report);
    return fixed_report;
  }

  auto fixed_asset_report = fixed_asset_reports.first().toMap();
  const auto fixed_validation = fixed_asset_report.value(QStringLiteral("validationReport")).toMap();
  const auto fixed_style_match = fixed_validation.value(QStringLiteral("styleMatch")).toMap();
  const auto still_needs_review =
      !fixed_validation.value(QStringLiteral("ok"), true).toBool() ||
      !fixed_asset_report.value(QStringLiteral("warnings")).toStringList().isEmpty() ||
      fixed_style_match.value(QStringLiteral("needsReview")).toBool();
  fixed_asset_report.insert(QStringLiteral("reviewIssueResolved"), !still_needs_review);
  fixed_asset_report.insert(QStringLiteral("reviewIssueResolution"), QStringLiteral("fixed"));
  fixed_asset_report.insert(QStringLiteral("reviewIssueResolvedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
  fixed_asset_report.insert(QStringLiteral("fixedFromReviewIssue"), true);

  auto merged_report = source_report;
  auto merged_asset_reports = source_asset_reports;
  auto merged = false;
  for (auto index = 0; index < merged_asset_reports.size(); ++index) {
    const auto candidate = merged_asset_reports.at(index).toMap();
    if (candidate.value(QStringLiteral("assetId")).toString() != trimmed_asset_id) {
      continue;
    }
    merged_asset_reports[index] = fixed_asset_report;
    merged = true;
    break;
  }
  if (!merged) {
    merged_asset_reports.append(fixed_asset_report);
  }
  merged_report.insert(QStringLiteral("assetReports"), merged_asset_reports);
  const auto merged_summary = batch_summary(
      merged_report.value(QStringLiteral("scope")).toString(),
      merged_report.value(QStringLiteral("assetIds")).toStringList(),
      merged_asset_reports,
      draftActive(),
      draftActive());
  merged_report.insert(QStringLiteral("batchSummary"), merged_summary);
  merged_report.insert(QStringLiteral("draftActive"), draftActive());
  merged_report.insert(QStringLiteral("lastReviewIssueAction"), fixed_report);

  fixed_report.insert(QStringLiteral("resolution"), QStringLiteral("fixed"));
  fixed_report.insert(QStringLiteral("stillNeedsReview"), still_needs_review);
  fixed_report.insert(QStringLiteral("batchSummary"), merged_summary);
  merged_report.insert(QStringLiteral("lastReviewIssueAction"), fixed_report);
  setStatus(still_needs_review ? QStringLiteral("Review issue repaired with warnings") : QStringLiteral("Review issue fixed"));
  setLastRunReport(merged_report);
  emit draftChanged();
  return fixed_report;
}

QVariantMap AgentEditController::fixAllReviewIssues() {
  if (!draftActive()) {
    auto report = result_map(false, {QStringLiteral("No active draft is available to repair.")});
    report.insert(QStringLiteral("mode"), QStringLiteral("review-issue-fix-all"));
    setLastRunReport(report);
    return report;
  }

  const auto source_report = last_run_report_;
  const auto review_summary = source_report.value(QStringLiteral("batchSummary"))
                                  .toMap()
                                  .value(QStringLiteral("reviewSummary"))
                                  .toMap();
  if (!review_summary.value(QStringLiteral("fixAllOutliersAvailable")).toBool()) {
    auto report = result_map(
        false,
        {QStringLiteral("Fix all is only available when there are multiple review issues and no failed photos.")});
    report.insert(QStringLiteral("mode"), QStringLiteral("review-issue-fix-all"));
    report.insert(QStringLiteral("reviewSummary"), review_summary);
    setLastRunReport(report);
    return report;
  }

  const auto review_issues = source_report.value(QStringLiteral("batchSummary"))
                                 .toMap()
                                 .value(QStringLiteral("reviewIssues"))
                                 .toList();
  QStringList asset_ids;
  for (const auto& issue_value : review_issues) {
    const auto issue = issue_value.toMap();
    const auto asset_id = issue.value(QStringLiteral("assetId")).toString().trimmed();
    if (!asset_id.isEmpty() && !asset_ids.contains(asset_id)) {
      asset_ids.append(asset_id);
    }
  }
  if (asset_ids.isEmpty()) {
    auto report = result_map(false, {QStringLiteral("No review issues are available to repair.")});
    report.insert(QStringLiteral("mode"), QStringLiteral("review-issue-fix-all"));
    setLastRunReport(report);
    return report;
  }

  QVariantList repair_reports;
  QStringList fixed_asset_ids;
  QStringList still_needs_review_asset_ids;
  QStringList failed_asset_ids;
  for (const auto& asset_id : asset_ids) {
    auto repair_report = fixReviewIssue(asset_id);
    repair_reports.append(repair_report);
    if (!repair_report.value(QStringLiteral("ok")).toBool()) {
      failed_asset_ids.append(asset_id);
      break;
    }
    fixed_asset_ids.append(asset_id);
    if (repair_report.value(QStringLiteral("stillNeedsReview")).toBool()) {
      still_needs_review_asset_ids.append(asset_id);
    }
  }

  auto final_report = last_run_report_;
  const auto final_summary = final_report.value(QStringLiteral("batchSummary")).toMap();
  QVariantMap action_report = result_map(failed_asset_ids.isEmpty());
  action_report.insert(QStringLiteral("mode"), QStringLiteral("review-issue-fix-all"));
  action_report.insert(QStringLiteral("requestedAssetIds"), asset_ids);
  action_report.insert(QStringLiteral("fixedAssetIds"), fixed_asset_ids);
  action_report.insert(QStringLiteral("fixedCount"), fixed_asset_ids.size());
  action_report.insert(QStringLiteral("failedAssetIds"), failed_asset_ids);
  action_report.insert(QStringLiteral("stillNeedsReviewAssetIds"), still_needs_review_asset_ids);
  action_report.insert(QStringLiteral("stillNeedsReviewCount"), still_needs_review_asset_ids.size());
  action_report.insert(QStringLiteral("repairReports"), repair_reports);
  action_report.insert(QStringLiteral("batchSummary"), final_summary);
  if (!failed_asset_ids.isEmpty()) {
    action_report.insert(
        QStringLiteral("errors"),
        QStringList{QStringLiteral("Unable to repair every review issue. Stopped at %1.").arg(failed_asset_ids.first())});
  }
  final_report.insert(QStringLiteral("lastReviewIssueAction"), action_report);
  setStatus(
      failed_asset_ids.isEmpty()
          ? QStringLiteral("Review issues fixed")
          : QStringLiteral("Review issue repair stopped"));
  setLastRunReport(final_report);
  emit draftChanged();
  return action_report;
}

QVariantMap AgentEditController::rollbackReviewIssue(const QString& asset_id) {
  const auto trimmed_asset_id = asset_id.trimmed();
  QStringList errors;
  if (trimmed_asset_id.isEmpty()) {
    errors.append(QStringLiteral("Review issue asset id is required."));
  }
  if (catalog_model_ == nullptr) {
    errors.append(QStringLiteral("Catalog model is not connected."));
  }
  if (!draft_original_recipes_.contains(trimmed_asset_id)) {
    errors.append(QStringLiteral("No active draft recipe is available to roll back for %1.").arg(trimmed_asset_id));
  }
  if (!errors.isEmpty()) {
    auto report = result_map(false, errors);
    report.insert(QStringLiteral("mode"), QStringLiteral("review-issue-rollback"));
    report.insert(QStringLiteral("assetId"), trimmed_asset_id);
    setLastRunReport(report);
    return report;
  }

  const auto restored_count =
      catalog_model_->applyRecipeJsonToAssetIds({trimmed_asset_id}, draft_original_recipes_.value(trimmed_asset_id));
  if (restored_count != 1) {
    auto report = result_map(false, {QStringLiteral("Unable to restore the original recipe for %1.").arg(trimmed_asset_id)});
    report.insert(QStringLiteral("mode"), QStringLiteral("review-issue-rollback"));
    report.insert(QStringLiteral("assetId"), trimmed_asset_id);
    setLastRunReport(report);
    return report;
  }
  draft_original_recipes_.remove(trimmed_asset_id);
  if (draft_original_recipes_.isEmpty()) {
    draft_run_id_.clear();
  }

  auto updated_report = last_run_report_;
  auto asset_reports = updated_report.value(QStringLiteral("assetReports")).toList();
  auto found = false;
  for (auto index = 0; index < asset_reports.size(); ++index) {
    auto asset_report = asset_reports.at(index).toMap();
    if (asset_report.value(QStringLiteral("assetId")).toString() != trimmed_asset_id) {
      continue;
    }
    found = true;
    asset_report.insert(QStringLiteral("status"), QStringLiteral("rolled_back"));
    asset_report.insert(QStringLiteral("reviewIssueResolved"), true);
    asset_report.insert(QStringLiteral("reviewIssueResolution"), QStringLiteral("rolled_back"));
    asset_report.insert(QStringLiteral("reviewIssueResolvedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    asset_reports[index] = asset_report;
    break;
  }
  if (!found) {
    asset_reports.append(QVariantMap{
        {QStringLiteral("assetId"), trimmed_asset_id},
        {QStringLiteral("status"), QStringLiteral("rolled_back")},
        {QStringLiteral("reviewIssueResolved"), true},
        {QStringLiteral("reviewIssueResolution"), QStringLiteral("rolled_back")},
        {QStringLiteral("reviewIssueResolvedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
    });
  }
  updated_report.insert(QStringLiteral("assetReports"), asset_reports);
  auto active_asset_ids = draft_original_recipes_.keys();
  std::sort(active_asset_ids.begin(), active_asset_ids.end());
  updated_report.insert(QStringLiteral("activeDraftAssetIds"), active_asset_ids);
  updated_report.insert(QStringLiteral("draftActive"), !active_asset_ids.isEmpty());
  const auto summary = batch_summary(
      updated_report.value(QStringLiteral("scope")).toString(),
      updated_report.value(QStringLiteral("assetIds")).toStringList(),
      asset_reports,
      !active_asset_ids.isEmpty(),
      !active_asset_ids.isEmpty());
  updated_report.insert(QStringLiteral("batchSummary"), summary);
  updated_report.insert(
      QStringLiteral("batchTransaction"),
      batch_transaction(
          draft_run_id_,
          updated_report.value(QStringLiteral("scope")).toString(),
          active_asset_ids,
          active_asset_ids.isEmpty() ? QStringLiteral("rolled_back") : QStringLiteral("drafted"),
          !active_asset_ids.isEmpty()));

  QVariantMap action_report = result_map(true);
  action_report.insert(QStringLiteral("mode"), QStringLiteral("review-issue-rollback"));
  action_report.insert(QStringLiteral("assetId"), trimmed_asset_id);
  action_report.insert(QStringLiteral("restoredCount"), restored_count);
  action_report.insert(QStringLiteral("resolution"), QStringLiteral("rolled_back"));
  action_report.insert(QStringLiteral("draftActive"), !active_asset_ids.isEmpty());
  action_report.insert(QStringLiteral("activeDraftAssetIds"), active_asset_ids);
  action_report.insert(QStringLiteral("batchSummary"), summary);
  updated_report.insert(QStringLiteral("lastReviewIssueAction"), action_report);
  setStatus(active_asset_ids.isEmpty() ? QStringLiteral("Rolled back") : QStringLiteral("Review issue rolled back"));
  setLastRunReport(updated_report);
  emit draftChanged();
  return action_report;
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
  auto enriched_report = report;
  const auto run_id = enriched_report.value(QStringLiteral("runId")).toString();
  const auto mode = enriched_report.value(QStringLiteral("mode")).toString();
  if (!run_id.trimmed().isEmpty() &&
      !mode.trimmed().isEmpty() &&
      !enriched_report.contains(QStringLiteral("runMemory"))) {
    const auto run_memory_report =
        write_agent_run_memory_note(AppBootstrap::appDataLocation(), selectedStyle(), enriched_report);
    enriched_report.insert(QStringLiteral("runMemory"), run_memory_report);
    enriched_report.insert(QStringLiteral("runMemoryNotePath"), run_memory_report.value(QStringLiteral("path")));
    if (!run_memory_report.value(QStringLiteral("ok")).toBool()) {
      auto warnings = enriched_report.value(QStringLiteral("warnings")).toStringList();
      warnings.append(run_memory_report.value(QStringLiteral("errors")).toStringList());
      enriched_report.insert(QStringLiteral("warnings"), warnings);
    }
    const auto issue_memory_report =
        write_agent_issue_memory_note(AppBootstrap::appDataLocation(), selectedStyle(), enriched_report);
    enriched_report.insert(QStringLiteral("issueMemory"), issue_memory_report);
    if (!issue_memory_report.value(QStringLiteral("skipped")).toBool()) {
      enriched_report.insert(
          QStringLiteral("issueMemoryNotePath"),
          issue_memory_report.value(QStringLiteral("path")));
    }
    if (!issue_memory_report.value(QStringLiteral("ok")).toBool()) {
      auto warnings = enriched_report.value(QStringLiteral("warnings")).toStringList();
      warnings.append(issue_memory_report.value(QStringLiteral("errors")).toStringList());
      enriched_report.insert(QStringLiteral("warnings"), warnings);
    }
  }
  refreshMemoryVaultSnapshot();
  last_run_report_ = enriched_report;
  const auto history_entry = run_history_entry(enriched_report);
  if (!history_entry.isEmpty()) {
    run_history_.prepend(history_entry);
    while (run_history_.size() > 20) {
      run_history_.removeLast();
    }
    emit runHistoryChanged();
  }
  emit lastRunReportChanged();
}

void AgentEditController::setPendingAgentInput(const QVariantMap& input) {
  if (pending_agent_input_ == input) {
    return;
  }
  pending_agent_input_ = input;
  emit pendingAgentInputChanged();
}

void AgentEditController::clearPendingAgentInput() {
  if (pending_agent_input_.isEmpty()) {
    return;
  }
  pending_agent_input_.clear();
  emit pendingAgentInputChanged();
}

void AgentEditController::refreshMemoryVaultSnapshot() const {
  memory_vault_snapshot_ = agent_memory_vault_snapshot(AppBootstrap::appDataLocation());
  memory_vault_snapshot_initialized_ = true;
}

void AgentEditController::setMcpStatusState(
    const QString& status,
    const QStringList& tool_names,
    bool available,
    const QString& last_error) {
  const auto normalized_status = status.trimmed().isEmpty() ? QStringLiteral("Unavailable") : status.trimmed();
  const auto normalized_error = last_error.trimmed();
  if (mcp_status_ == normalized_status &&
      mcp_tool_names_ == tool_names &&
      mcp_available_ == available &&
      mcp_last_error_ == normalized_error) {
    return;
  }
  mcp_status_ = normalized_status;
  mcp_tool_names_ = tool_names;
  mcp_available_ = available;
  mcp_last_error_ = normalized_error;
  emit mcpStatusChanged();
}

void AgentEditController::setMcpTelemetryState(
    int connected_client_count,
    int total_client_count,
    const QString& last_tool_call_name,
    const QString& last_tool_call_status,
    const QString& last_tool_call_at) {
  const auto bounded_connected = std::max(0, connected_client_count);
  const auto bounded_total = std::max(0, total_client_count);
  const auto normalized_name = last_tool_call_name.trimmed();
  const auto normalized_status = last_tool_call_status.trimmed();
  const auto normalized_at = last_tool_call_at.trimmed();
  if (mcp_connected_client_count_ == bounded_connected &&
      mcp_total_client_count_ == bounded_total &&
      mcp_last_tool_call_name_ == normalized_name &&
      mcp_last_tool_call_status_ == normalized_status &&
      mcp_last_tool_call_at_ == normalized_at) {
    return;
  }
  mcp_connected_client_count_ = bounded_connected;
  mcp_total_client_count_ = bounded_total;
  mcp_last_tool_call_name_ = normalized_name;
  mcp_last_tool_call_status_ = normalized_status;
  mcp_last_tool_call_at_ = normalized_at;
  emit mcpStatusChanged();
}

void AgentEditController::setModelInstallState(
    bool running,
    const QString& status,
    const QString& output,
    const QVariantMap& progress) {
  const auto normalized_status = status.trimmed().isEmpty() ? QStringLiteral("Idle") : status.trimmed();
  if (modelInstallRunning() == running &&
      model_install_status_ == normalized_status &&
      model_install_output_ == output &&
      model_install_progress_ == progress) {
    return;
  }
  model_install_status_ = normalized_status;
  model_install_output_ = output;
  model_install_progress_ = progress;
  emit modelInstallChanged();
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

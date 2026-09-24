#include "search_edit_workflow.h"
#include "catalog_asset_model.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSet>
#include <QUuid>

namespace photo_editor::desktop {
namespace {
QByteArray source_snapshot(const QVariantMap& asset) {
  const QFileInfo source(asset.value("filePath").toString());
  if (!source.isFile() || !source.isReadable() || !asset.value("online").toBool()) return {};
  const QVariantMap identity{{"id", asset.value("assetId")}, {"path", source.canonicalFilePath()},
      {"size", source.size()}, {"mtime", source.lastModified().toMSecsSinceEpoch()},
      {"birth", source.birthTime().toMSecsSinceEpoch()},
      {"adjustments", asset.value("adjustmentPatchJson")}};
  return QCryptographicHash::hash(QJsonDocument::fromVariant(identity).toJson(QJsonDocument::Compact),
                                  QCryptographicHash::Sha256);
}
}

SearchEditWorkflow::SearchEditWorkflow(CatalogAssetModel* catalog, QObject* parent)
    : QObject(parent), catalog_(catalog) {
  connect(&search_, &VisualSearchController::searchFailed, this, [this](const QString& message) {
    if (phase_ != QStringLiteral("searching")) return;
    phase_ = QStringLiteral("failed");
    confirmation_token_.clear();
    status_ = message;
    emit changed();
  });
  connect(&search_, &VisualSearchController::changed, this, [this] {
    if (phase_ == QStringLiteral("searching")) status_ = search_.status();
    emit changed();
  });
  connect(&search_, &VisualSearchController::batchFinished, this, [this] {
    if (phase_ != QStringLiteral("searching")) return;
    phase_ = QStringLiteral("confirm");
    confirmation_token_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    status_ = search_.resultIds().isEmpty()
        ? QStringLiteral("No matches in this scan. Nothing was edited.")
        : QStringLiteral("Check these photos and the edit instructions. Nothing has been edited yet.");
    emit changed();
  });
}

void SearchEditWorkflow::setDraftBackend(std::function<bool()> busy,
    std::function<QVariantMap(const QStringList&, const QString&)> start) {
  busy_ = std::move(busy);
  start_draft_ = std::move(start);
}

QVariantMap SearchEditWorkflow::reject(const QString& message) {
  status_ = message;
  emit changed();
  return {{"ok", false}, {"errors", QStringList{message}}};
}

QVariantMap SearchEditWorkflow::start(const QString& instruction, const QVariantMap& model) {
  if (catalog_ == nullptr || !start_draft_) return reject(QStringLiteral("Search-and-edit is unavailable in this runtime."));
  if ((busy_ && busy_()) || phase_ == QStringLiteral("drafting"))
    return reject(QStringLiteral("Finish or discard the current agent work first."));
  confirmation_token_.clear();
  confirmed_ids_.clear();
  request_id_.clear();
  source_snapshots_.clear();
  const auto assets = catalog_->visibleAssets();
  for (const auto& value : assets) {
    const auto asset = value.toMap();
    source_snapshots_.insert(asset.value("assetId").toString(), source_snapshot(asset));
  }
  phase_ = QStringLiteral("searching");
  search_.searchForEdit(instruction, assets, model);
  status_ = search_.status();
  emit changed();
  return {{"ok", search_.running()}, {"accepted", search_.running()}, {"requiresTargetConfirmation", true}};
}

void SearchEditWorkflow::scanMore() {
  if (phase_ != QStringLiteral("confirm") || !search_.hasMore()) return;
  confirmation_token_.clear();
  phase_ = QStringLiteral("searching");
  search_.searchMore();
  emit changed();
}

void SearchEditWorkflow::cancel() {
  // Draft cancellation belongs to AgentEditController and its rollback contract.
  if (phase_ == QStringLiteral("drafting")) return;
  confirmation_token_.clear();
  phase_ = QStringLiteral("idle");
  search_.cancel();
  status_ = QStringLiteral("Search stopped. No new draft was started.");
  emit changed();
}

QVariantMap SearchEditWorkflow::confirmTargets(const QString& token, const QStringList& ids,
                                              const QString& edit_instructions) {
  if (phase_ != QStringLiteral("confirm") || search_.running() || token.isEmpty() || token != confirmation_token_)
    return reject(QStringLiteral("This search confirmation has expired. Review the latest results."));
  if (!start_draft_ || (busy_ && busy_())) return reject(QStringLiteral("Finish or discard the current agent work first."));
  const auto instructions = edit_instructions.trimmed();
  if (ids.isEmpty() || instructions.isEmpty() || instructions.size() > 1000)
    return reject(QStringLiteral("Choose at least one photo and provide edit instructions (up to 1,000 characters)."));
  const auto matches = search_.resultIds();
  QSet<QString> seen;
  for (const auto& id : ids) {
    if (seen.contains(id) || !matches.contains(id) || !source_snapshots_.contains(id) ||
        source_snapshots_.value(id).isEmpty() || source_snapshot(catalog_->assetById(id)) != source_snapshots_.value(id)) {
      confirmation_token_.clear();
      phase_ = QStringLiteral("stale");
      return reject(QStringLiteral("A target, original file, or edit changed since search began. Search again before editing."));
    }
    seen.insert(id);
  }
  // Consume approval before dispatch so reentrant UI/tool activity cannot reuse it.
  confirmation_token_.clear();
  phase_ = QStringLiteral("drafting");
  confirmed_ids_ = ids;
  const auto result = start_draft_(ids, instructions);
  if (!result.value("accepted").toBool()) {
    phase_ = QStringLiteral("stale");
    return reject(result.value("errors").toStringList().join(QStringLiteral("; ")));
  }
  request_id_ = result.value("requestId").toString();
  status_ = QStringLiteral("Creating a reversible draft on the confirmed photos only.");
  emit changed();
  emit draftStarted(ids);
  return result;
}

void SearchEditWorkflow::draftFinished(const QString& request_id, const QVariantMap& report) {
  if (phase_ != QStringLiteral("drafting") || request_id != request_id_) return;
  phase_ = QStringLiteral("review");
  status_ = report.value("ok").toBool() ? QStringLiteral("Review the rendered draft before keeping it.")
      : QStringLiteral("The agent needs attention. Open its review to continue or discard the draft.");
  emit changed();
}

QVariantMap SearchEditWorkflow::snapshot() const {
  QVariantList matches;
  for (const auto& value : search_.results()) {
    const auto item = value.toMap();
    matches.append(QVariantMap{{"assetId", item.value("assetId")}, {"score", item.value("score")}, {"reason", item.value("reason")}});
  }
  // No confirmation token, file paths, image bytes or authority to approve via MCP.
  return {{"phase", phase_}, {"status", status_}, {"running", search_.running()},
      {"plan", search_.searchPlan()}, {"matches", matches}, {"inspected", search_.inspected()},
      {"total", search_.total()}, {"skipped", search_.skipped()}, {"hasMore", search_.hasMore()},
      {"confirmedIds", confirmed_ids_}, {"requiresLocalConfirmation", true}};
}
}  // namespace photo_editor::desktop

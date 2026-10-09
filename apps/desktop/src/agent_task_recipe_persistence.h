#pragma once

#include "agent_task_coordinator.h"
#include "catalog_asset_model.h"
#include "catalog_helper_payload.h"
#include "editor_preview_controller.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

namespace photo_editor::desktop {

inline QByteArray agent_recipe_guard_snapshot(const QByteArray& payload, const QString& asset_id) {
  const auto envelope = QJsonDocument::fromJson(payload).object();
  const auto arguments = envelope.value("arguments").toArray();
  if (envelope.value("schema") != QStringLiteral("photo-editor-catalog-arguments:v1") ||
      arguments.size() < 2 || arguments.at(0) != QStringLiteral("--agent-owned-v1") ||
      !arguments.at(1).isString()) return {};
  const auto snapshot = QJsonDocument::fromJson(arguments.at(1).toString().toUtf8());
  const auto object = snapshot.object();
  if (!snapshot.isObject() || object.value("schema") != QStringLiteral("agent-asset-recipe:v1") ||
      object.value("state") != QStringLiteral("present") || object.value("asset_id") != asset_id ||
      object.value("source_path").toString().isEmpty() ||
      object.value("source_sha256").toString().size() != 64) return {};
  return snapshot.toJson(QJsonDocument::Compact);
}

inline QByteArray agent_recipe_guarded_payload(const QByteArray& payload, const QByteArray& before) {
  auto envelope = QJsonDocument::fromJson(payload).object();
  auto arguments = envelope.value("arguments").toArray();
  if (envelope.value("schema") != QStringLiteral("photo-editor-catalog-arguments:v1") ||
      arguments.isEmpty() || arguments.size() > 30 ||
      arguments.at(0) == QStringLiteral("--agent-owned-v1") ||
      !QJsonDocument::fromJson(before).isObject()) return {};
  arguments.prepend(QString::fromUtf8(before));
  arguments.prepend(QStringLiteral("--agent-owned-v1"));
  envelope.insert("arguments", arguments);
  const auto guarded = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
  return guarded.size() <= kMaxCatalogHelperPayloadBytes ? guarded : QByteArray{};
}

// Decode the exact prepared helper envelope, not a fresh serialization of the
// selected document. Receipt comparisons must use what the helper actually got.
inline QVariantMap agent_recipe_fields_from_prepared_payload(const QByteArray& payload,
                                                             const QString& asset_id) {
  const auto object = QJsonDocument::fromJson(payload).object();
  if (object.value("schema") != QStringLiteral("photo-editor-catalog-arguments:v1")) return {};
  auto arguments = object.value("arguments").toArray();
  for (const auto& argument : arguments) if (!argument.isString()) return {};
  if (!arguments.isEmpty() && arguments.at(0) == QStringLiteral("--agent-owned-v1")) {
    if (agent_recipe_guard_snapshot(payload, asset_id).isEmpty()) return {};
    arguments.removeAt(0);
    arguments.removeAt(0);
  }
  const bool legacy = arguments.size() == 3 && arguments.at(0) == QStringLiteral("--legacy-v1");
  const bool canonical = arguments.size() == 5 || arguments.size() == 13;
  if ((!legacy && !canonical) || arguments.at(legacy ? 1 : 0).toString() != asset_id) return {};
  const auto legacy_document = QJsonDocument::fromJson(arguments.at(legacy ? 2 : 1).toString().toUtf8());
  if (!legacy_document.isObject()) return {};
  QJsonValue canonical_recipe(QJsonValue::Null);
  if (canonical) {
    const auto provenance = QJsonDocument::fromJson(arguments.at(4).toString().toUtf8());
    if (!provenance.isObject() || arguments.at(2).toString().isEmpty() || arguments.at(3).toString().isEmpty()) return {};
    canonical_recipe = QJsonObject{{"recipe_payload", arguments.at(2)},
        {"cache_key", arguments.at(3)}, {"provenance_payload", arguments.at(4)}};
  }
  return QJsonObject{{"legacyRecipe", legacy_document.object()},
      {"canonicalRecipe", canonical_recipe}}.toVariantMap();
}

// Own the asynchronous DB half of native recipe saves. No successful launch is
// a successful save; only the helper's terminal receipt plus exact DB readback
// can advance the journal. There is no elapsed-time deadline or nested event loop.
class AgentTaskRecipePersistence final : public QObject {
 public:
  AgentTaskRecipePersistence(EditorPreviewController& preview, CatalogAssetModel& catalog,
                            AgentTaskCoordinator& tasks)
      : preview_(preview), catalog_(catalog), tasks_(tasks) {
    connect(&preview_, &EditorPreviewController::recipeSaveStateChanged, this, [this] { scheduleSettle(); });
    connect(&catalog_, &CatalogAssetModel::agentTaskRecipePersistenceChanged, this,
        [this](const QString&) { scheduleSettle(); });
  }
  ~AgentTaskRecipePersistence() override {
    preview_.setAgentRecipePersistenceHandler({});
    catalog_.setAgentRecipePersistenceHandler({});
  }
  void setSettlementHandler(std::function<void()> handler) { settled_ = std::move(handler); }
  bool pending() const { return !receipt_.isEmpty(); }
  QString error() const { return error_; }

  QVariantMap handle(const QString& stage, const QVariantMap& context) {
    const auto task = context.value("taskId").toString();
    const auto asset = context.value("assetId").toString();
    const auto payload = context.value("payload").toByteArray();
    const auto catalog_revision = context.value("sourceOwner") == QStringLiteral("catalog")
        ? context.value("revisionId").toString() : QString{};
    const auto same_attempt = [&]() {
      return receipt_.value("taskId") == task && receipt_.value("assetId") == asset &&
          receipt_.value("sourceOwner") == context.value("sourceOwner") &&
          receipt_.value("payload").toByteArray() == payload &&
          receipt_.value("revisionId").toString() == context.value("revisionId").toString() &&
          receipt_.value("attemptId").toString() == context.value("attemptId").toString();
    };
    if (stage == QStringLiteral("accepted")) {
      if (!same_attempt())
        return fail("The accepted save does not match its prepared immutable revision.");
      const auto accepted = tasks_.journal()->complete(receipt_.value("stepId").toLongLong(),
          receipt_.value("before").toByteArray(), {{"ok", true}, {"accepted", true}}, true);
      if (!accepted.value("ok").toBool()) error_ = accepted.value("error").toString();
      return accepted;
    }
    if (stage == QStringLiteral("finished")) {
      if (!same_attempt()) return fail("The recipe save receipt belongs to another revision.");
      receipt_.insert("helperSucceeded", context.value("ok").toBool());
      if (!context.value("ok").toBool()) receipt_.insert("helperFailed", true);
      scheduleSettle();
      return {{"ok", true}, {"pending", true}};
    }
    if (stage != QStringLiteral("prepare") && stage != QStringLiteral("guard"))
      return fail("Unknown recipe persistence stage.");
    if (task.isEmpty() || tasks_.journal()->taskId() != task || !tasks_.journal()->active())
      return fail("Recover the original agent task before saving its pending revision.");
    const auto resource = QStringLiteral("catalog-recipe:%1").arg(asset);
    if (tasks_.journal()->checkpointHash(resource).isEmpty())
      return fail("This task has no catalog recipe owner; its pending edit was preserved.");
    if (pending()) {
      return fail("Waiting for the preceding owned recipe save to settle.");
    }
    if (preview_.recipeSaveProcessForClose()->state() != QProcess::NotRunning)
      return fail("Waiting for the running native recipe producer to settle.");
    const auto fields = agent_recipe_fields_from_prepared_payload(payload, asset);
    const auto before = catalog_.agentTaskRecipeResourceAdapter(asset, task).read();
    if (fields.isEmpty() || before.isEmpty() || !tasks_.journal()->matchesCheckpoint(resource, before))
      return fail("The stored recipe changed outside this task; current edits were preserved.");
    if (stage == QStringLiteral("guard")) {
      const auto guarded = agent_recipe_guarded_payload(payload, before);
      if (guarded.isEmpty()) return fail("The owned recipe's exact commit guard could not be prepared.");
      return {{"ok", true}, {"payload", guarded}};
    }
    if (agent_recipe_guard_snapshot(payload, asset) != before)
      return fail("The stored recipe changed after its commit guard; current edits were preserved.");
    if (!catalog_.registerAgentTaskRecipeCheckpoint(task, asset, before, fields,
            catalog_revision))
      return fail("Waiting for catalog saves before admitting the owned recipe revision.");
    const auto prepared = tasks_.journal()->prepare(resource, QStringLiteral("persist_native_recipe"),
        {{"payloadSha256", QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex())}}, before);
    if (!prepared.value("ok").toBool()) {
      static_cast<void>(catalog_.cancelAgentTaskRecipeCheckpoint(task, asset, before));
      return prepared;
    }
    receipt_ = context;
    receipt_.insert("resourceId", resource);
    receipt_.insert("stepId", prepared.value("stepId"));
    receipt_.insert("before", before);
    error_.clear();
    return {{"ok", true}};
  }

 private:
  QVariantMap fail(const QString& message) { return {{"ok", false}, {"error", message}}; }
  void scheduleSettle() {
    if (scheduled_) return;
    scheduled_ = true;
    QTimer::singleShot(0, this, [this] { scheduled_ = false; settle(); });
  }
  void settle() {
    // Pending future revisions are not running producers. Requiring their
    // queues to empty here would deadlock the owner that must admit them next.
    if (preview_.recipeSaveProcessForClose()->state() != QProcess::NotRunning) return;
    const auto catalog_revision = receipt_.value("sourceOwner") == QStringLiteral("catalog")
        ? receipt_.value("revisionId").toString() : QString{};
    if (pending() && receipt_.value("helperFailed").toBool()) {
      if (catalog_.cancelAgentTaskRecipeCheckpoint(receipt_.value("taskId").toString(),
              receipt_.value("assetId").toString(), receipt_.value("before").toByteArray(),
              catalog_revision)) {
        const auto message = QStringLiteral("The native save failed without changing its stored recipe; the immutable revision remains queued.");
        const auto abandoned = tasks_.journal()->pending(receipt_.value("resourceId").toString())
            ? tasks_.journal()->abandonPending(receipt_.value("resourceId").toString(), message)
            : tasks_.journal()->complete(receipt_.value("stepId").toLongLong(),
                receipt_.value("before").toByteArray(), {{"ok", false}, {"error", message}});
        if (abandoned.value("ok").toBool()) { receipt_.clear(); error_.clear(); }
        else error_ = abandoned.value("error").toString();
      } else {
        error_ = QStringLiteral("The failed save could not be reconciled with its original checkpoint. Current edits and the recovery journal were preserved.");
      }
    }
    if (pending() && receipt_.value("helperSucceeded").toBool() &&
        (!catalog_revision.isEmpty() ||
         catalog_.agentTaskRecipePersistenceSettled(receipt_.value("assetId").toString()))) {
      const auto bytes = catalog_.completeAgentTaskRecipeCheckpoint(
          receipt_.value("taskId").toString(), receipt_.value("assetId").toString(),
          catalog_revision);
      if (bytes.isEmpty()) {
        error_ = QStringLiteral("The completed save does not match its exact owned recipe receipt. Current data was preserved.");
      } else {
        const auto completed = tasks_.journal()->settle(receipt_.value("resourceId").toString(), bytes, true);
        if (completed.value("ok").toBool()) { receipt_.clear(); error_.clear(); }
        else error_ = completed.value("error").toString();
      }
    }
    if (settled_) settled_();
  }
  EditorPreviewController& preview_;
  CatalogAssetModel& catalog_;
  AgentTaskCoordinator& tasks_;
  QVariantMap receipt_;
  QString error_;
  bool scheduled_ = false;
  std::function<void()> settled_;
};

}  // namespace photo_editor::desktop

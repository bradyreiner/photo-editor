#pragma once

// Opt-in, real-model full-workflow proof driver. Main may own one instance in
// its marked private-runtime proof mode after the production controllers and
// QML window are wired. This helper drives the visible Agent terminal buttons;
// it does not inject planning or editing handlers and never scripts edit tools.

#include "agent_edit_controller.h"
#include "agent_session_controller.h"
#include "catalog_asset_model.h"
#include "edit_recipe_state.h"
#include "editor_preview_controller.h"
#include "photo_paint_surface.h"
#include "photo_render_utils.h"
#include "photo_text_rasterizer.h"
#include "agent_text_glyph_mask.h"

#include <QColor>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QProcessEnvironment>
#include <QPainter>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRect>
#include <QSet>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <optional>
#include <utility>

namespace photo_editor::desktop {

struct AgentFullWorkflowProofOptions final {
  QString fixture_root;
  QString asset_id;
  QString source_path;
  QString installed_model_id;
  QString initial_prompt;
  QString revision_prompt;
  QString exact_copy;
  QString revision_exact_copy;
  // When populated, the proof expects these exact editable text layers in
  // order (kicker, headline, footer) and runs without the cutout-only checks.
  QStringList cover_copy;
  QStringList revision_cover_copy;
  int minimum_output_width = 1600;
  int minimum_output_height = 900;
  int target_aspect_width = 16;
  int target_aspect_height = 9;
  // Top-to-bottom semantic roles expected from the approved composition, for
  // example {"cutout", "text"} when the subject must cover the headline.
  QStringList top_to_bottom_roles;
  QString test_database_environment = QStringLiteral(
      "PHOTO_EDITOR_AGENT_WORKFLOW_TEST_DATABASE_URL");
  std::function<void(const QVariantMap&)> finished;
};

class AgentFullWorkflowRuntimeProof final : public QObject {
 public:
  AgentFullWorkflowRuntimeProof(
      QQuickWindow* window,
      AgentSessionController* session,
      AgentEditController* edit,
      EditorPreviewController* preview,
      CatalogAssetModel* catalog,
      AgentFullWorkflowProofOptions options,
      QObject* parent = nullptr)
      : QObject(parent),
        window_(window),
        session_(session),
        edit_(edit),
        preview_(preview),
        catalog_(catalog),
        options_(std::move(options)) {
    timer_.setInterval(200);
    QObject::connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
    QObject::connect(session_, &AgentSessionController::stateChanged, this, [this] { advance(); });
    QObject::connect(edit_, &AgentEditController::draftChanged, this, [this] { advance(); });
    QObject::connect(edit_, &AgentEditController::taskTransactionChanged, this, [this] { advance(); });
    QObject::connect(preview_, &EditorPreviewController::recipeSaveStateChanged, this, [this] { advance(); });
    if (window_) {
      QObject::connect(window_, &QQuickWindow::frameSwapped, this, [this] {
        ++presented_frame_serial_;
      }, Qt::QueuedConnection);
    }
  }

  [[nodiscard]] QVariantMap start() {
    if (started_) return failure(QStringLiteral("The full-workflow proof was already started."));
    started_ = true;
    if (const auto error = validatePrivateFixture(); !error.isEmpty()) {
      fail(error);
      return failure(error);
    }
    edit_->setSelectedModelId(options_.installed_model_id);
    if (edit_->selectedModelId() != options_.installed_model_id) {
      const auto error = QStringLiteral("The explicitly selected installed model is unavailable.");
      fail(error);
      return failure(error);
    }
    const auto configured_model = installedLocalModel(options_.installed_model_id);
    prior_model_probe_at_utc_ = configured_model.value(QStringLiteral("lastProbeAtUtc")).toString();
    model_probe_requested_at_utc_ = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    edit_->probeSelectedModelAsync(true);
    phase_ = Phase::WaitingForModelProbe;
    static_cast<void>(writeCheckpoint(QStringLiteral("armed")));
    timer_.start();
    advance();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("status"), QStringLiteral("running")},
        {QStringLiteral("receiptPath"), receipt_path_}};
  }

  [[nodiscard]] bool finished() const { return finished_; }
  [[nodiscard]] QVariantMap receipt() const { return evidence_.toVariantMap(); }

 private:
  enum class PresentedCapturePurpose {
    Workspace,
    Request,
    PlanningCancelled,
    Plan,
    Review,
    WholeTaskReverted,
  };

  enum class PresentedCaptureResult {
    Captured,
    Pending,
    Failed,
  };

  struct PendingPresentedCapture final {
    bool active = false;
    bool frame_request_sent = false;
    bool window_activation_requested = false;
    QJsonObject window_activation_before;
    QString name;
    PresentedCapturePurpose purpose = PresentedCapturePurpose::Request;
    QString last_blocker;
    qint64 started_at_ms = 0;
    qint64 minimum_capture_at_ms = 0;
    quint64 frame_serial_before_request = 0;
  };

  enum class Phase {
    WaitingForModelProbe,
    WaitingForAsset,
    BaselineExport,
    OpenTerminal,
    PlanningCancel,
    PlanningCancelSettled,
    InitialPlan,
    InitialExecution,
    InitialPersistence,
    RevisionPlan,
    RevisionExecution,
    RevisionPersistence,
    EditedExport,
    RefreshCatalog,
    ReopenAsset,
    ReopenedExport,
    Revert,
    RevertedExport,
    Done,
  };

  [[nodiscard]] static QString sha256(const QByteArray& bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
  }

  [[nodiscard]] QVariantMap failure(const QString& error) const {
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
  }

  [[nodiscard]] bool isEditorialCover() const {
    return !options_.cover_copy.isEmpty();
  }

  [[nodiscard]] QStringList expectedCoverCopy(const QString& label) const {
    if (!isEditorialCover()) return {};
    return label == QStringLiteral("revision") || label == QStringLiteral("revised")
        ? options_.revision_cover_copy : options_.cover_copy;
  }

  [[nodiscard]] QString canonicalInsideRoot(const QString& path) const {
    const auto root = QFileInfo(options_.fixture_root).canonicalFilePath();
    const auto candidate = QFileInfo(path).canonicalFilePath();
    if (root.isEmpty() || candidate.isEmpty()) return {};
    const auto relative = QDir(root).relativeFilePath(candidate);
    if (relative.isEmpty() || relative == QStringLiteral("..") ||
        relative.startsWith(QStringLiteral("../")) || QDir::isAbsolutePath(relative)) return {};
    return candidate;
  }

  [[nodiscard]] QString validatePrivateFixture() {
    if (!window_ || !session_ || !edit_ || !preview_ || !catalog_)
      return QStringLiteral("The actual application proof controllers or window are unavailable.");
    const auto environment = QProcessEnvironment::systemEnvironment();
    QStringList fake_runtime_overrides;
    for (const auto& key : environment.keys()) {
      const auto normalized = key.toUpper();
      const auto relevant_fake_override = normalized.contains(QStringLiteral("FAKE")) &&
          (normalized.contains(QStringLiteral("MODEL")) ||
           normalized.contains(QStringLiteral("AI")) ||
           normalized.contains(QStringLiteral("VISION")) ||
           normalized.contains(QStringLiteral("SEGMENTATION")) ||
           normalized.contains(QStringLiteral("SELECTION")) ||
           normalized.contains(QStringLiteral("OLLAMA")));
      if (relevant_fake_override) fake_runtime_overrides.append(key);
    }
    if (!fake_runtime_overrides.isEmpty()) {
      fake_runtime_overrides.sort(Qt::CaseInsensitive);
      return QStringLiteral("The real-model workflow proof refuses fake model or vision runtime overrides: %1")
          .arg(fake_runtime_overrides.join(QStringLiteral(", ")));
    }
    const QFileInfo root_info(options_.fixture_root);
    const auto root = root_info.canonicalFilePath();
    const auto config_path = QDir(root).filePath(QStringLiteral("agent-workflow-private-runtime.json"));
    if (!root_info.isDir() || root.isEmpty() || !root_info.isAbsolute() ||
        QFileInfo(config_path).isSymLink() || !QFileInfo(config_path).isFile())
      return QStringLiteral("The opt-in proof requires an absolute private root with its runtime contract file.");
    options_.fixture_root = root;
    receipt_path_ = QDir(root).filePath(QStringLiteral("workflow-receipt.json"));
    if (QFileInfo::exists(receipt_path_))
      return QStringLiteral("The proof refuses to overwrite an earlier receipt; provide a fresh private runtime root.");
    evidence_ = QJsonObject{
        {QStringLiteral("schema"), QStringLiteral("agent-full-workflow-runtime-proof-v1")},
        {QStringLiteral("status"), QStringLiteral("in_progress")},
        {QStringLiteral("scope"), QStringLiteral("actual installed local model, visible planning Stop and retry, QML session, AgentEdit, Catalog/Preview save-reopen and whole-task Revert")},
        {QStringLiteral("fixtureRoot"), root},
        {QStringLiteral("assetId"), options_.asset_id},
        {QStringLiteral("sourcePath"), options_.source_path},
        {QStringLiteral("installedModelId"), options_.installed_model_id},
        {QStringLiteral("processRestartProven"), false},
        {QStringLiteral("stopBehaviorProven"), false},
    };
    static_cast<void>(writeCheckpoint(QStringLiteral("fixture_root_admitted")));
    QFile config_file(config_path);
    if (!config_file.open(QIODevice::ReadOnly))
      return QStringLiteral("Could not read the private runtime contract.");
    const auto config = QJsonDocument::fromJson(config_file.readAll()).object();
    const auto database_from_config = config.value(QStringLiteral("databaseUrl")).toString().trimmed();
    if (config.value(QStringLiteral("schema")).toString() != QStringLiteral("agent-workflow-private-runtime:v1") ||
        config.value(QStringLiteral("runtimeRoot")).toString() != root ||
        config.value(QStringLiteral("assetId")).toString() != options_.asset_id ||
        QFileInfo(config.value(QStringLiteral("sourcePath")).toString()).canonicalFilePath() !=
            QFileInfo(options_.source_path).canonicalFilePath() ||
        config.value(QStringLiteral("modelId")).toString() != options_.installed_model_id ||
        config.value(QStringLiteral("brief")).toString() != options_.initial_prompt ||
        config.value(QStringLiteral("revision")).toString() != options_.revision_prompt)
      return QStringLiteral("The supplied asset, source, model and briefs do not match the marked private runtime contract.");
    QStringList configured_roles;
    for (const auto& value : config.value(QStringLiteral("topToBottomRoles")).toArray())
      configured_roles.append(value.toString());
    QStringList configured_cover_copy;
    for (const auto& value : config.value(QStringLiteral("coverCopy")).toArray())
      configured_cover_copy.append(value.toString());
    QStringList configured_revision_cover_copy;
    for (const auto& value : config.value(QStringLiteral("revisionCoverCopy")).toArray())
      configured_revision_cover_copy.append(value.toString());
    if (config.value(QStringLiteral("exactCopy")).toString() != options_.exact_copy ||
        config.value(QStringLiteral("revisionExactCopy")).toString() != options_.revision_exact_copy ||
        configured_roles != options_.top_to_bottom_roles ||
        configured_cover_copy != options_.cover_copy ||
        configured_revision_cover_copy != options_.revision_cover_copy)
      return QStringLiteral("The exact copy or intended layer order differs from the marked private runtime contract.");
    const auto source = canonicalInsideRoot(options_.source_path);
    if (source.isEmpty() || !QFileInfo(source).isFile() || options_.asset_id.trimmed().isEmpty())
      return QStringLiteral("The imported photo ID and source must resolve inside the marked fixture root.");
    options_.source_path = source;
    const auto db = qEnvironmentVariable(options_.test_database_environment.toLocal8Bit().constData()).trimmed();
    const QUrl db_url(db);
    const auto db_name = db_url.path().section(QLatin1Char('/'), -1);
    if (db.isEmpty() || database_from_config != db || qEnvironmentVariable("PHOTO_EDITOR_DATABASE_URL") != db ||
        !db_url.isValid() || !QStringList{QStringLiteral("postgres"), QStringLiteral("postgresql")}
            .contains(db_url.scheme(), Qt::CaseInsensitive) ||
        db_url.host() != QStringLiteral("127.0.0.1") || db_url.port() <= 0 ||
        !db_name.contains(QStringLiteral("test"), Qt::CaseInsensitive))
      return QStringLiteral("The full-workflow proof requires the same explicitly supplied private PostgreSQL test database for the app and proof.");
    const auto app_data = QFileInfo(qEnvironmentVariable("PHOTO_EDITOR_APP_DATA_DIR")).canonicalFilePath();
    if (app_data.isEmpty() || app_data != QDir(root).filePath(QStringLiteral("runtime")) || !QFileInfo(app_data).isDir())
      return QStringLiteral("The application data directory is not isolated under the marked proof root.");
    if (options_.initial_prompt.trimmed().isEmpty() || options_.revision_prompt.trimmed().isEmpty() ||
        options_.exact_copy.isEmpty() || options_.revision_exact_copy.isEmpty() || options_.top_to_bottom_roles.isEmpty() ||
        !options_.top_to_bottom_roles.contains(QStringLiteral("text")) ||
        (options_.cover_copy.isEmpty() && !options_.top_to_bottom_roles.contains(QStringLiteral("cutout"))) ||
        ((!options_.cover_copy.isEmpty() || !options_.revision_cover_copy.isEmpty()) &&
         (options_.cover_copy.size() != 3 || options_.revision_cover_copy.size() != 3 ||
          options_.cover_copy != options_.revision_cover_copy ||
          options_.cover_copy.at(1) != options_.exact_copy ||
          options_.revision_cover_copy.at(1) != options_.revision_exact_copy)) ||
        options_.minimum_output_width <= 0 || options_.minimum_output_height <= 0 ||
        options_.target_aspect_width <= 0 || options_.target_aspect_height <= 0)
      return QStringLiteral("The proof requires a complete brief, revision, exact copy, and intended cutout/text order.");
    const auto model = installedLocalModel(options_.installed_model_id);
    if (model.isEmpty())
      return QStringLiteral("The requested model is not cataloged as an installed local Ollama model.");
    const auto artifact_root = QDir(root).filePath(QStringLiteral("workflow-evidence"));
    if (!QDir().mkpath(artifact_root))
      return QStringLiteral("Could not create evidence inside the marked private fixture root.");
    screenshot_root_ = QDir(artifact_root).filePath(QStringLiteral("screenshots"));
    output_root_ = QDir(artifact_root).filePath(QStringLiteral("outputs"));
    if (!QDir().mkpath(screenshot_root_) || !QDir().mkpath(output_root_))
      return QStringLiteral("Could not create private screenshot and export directories.");
    evidence_.insert(QStringLiteral("appDataPath"), app_data);
    evidence_.insert(QStringLiteral("testDatabaseName"), db_name);
    evidence_.insert(QStringLiteral("assetId"), options_.asset_id);
    evidence_.insert(QStringLiteral("sourcePath"), source);
    evidence_.insert(QStringLiteral("installedModelId"), options_.installed_model_id);
    evidence_.insert(QStringLiteral("configuredModelMetadata"), QJsonObject::fromVariantMap(model));
    evidence_.insert(QStringLiteral("initialPrompt"), options_.initial_prompt);
    evidence_.insert(QStringLiteral("revisionPrompt"), options_.revision_prompt);
    evidence_.insert(QStringLiteral("exactCopy"), options_.exact_copy);
    evidence_.insert(QStringLiteral("revisionExactCopy"), options_.revision_exact_copy);
    evidence_.insert(QStringLiteral("coverCopy"), QJsonArray::fromStringList(options_.cover_copy));
    evidence_.insert(QStringLiteral("revisionCoverCopy"), QJsonArray::fromStringList(options_.revision_cover_copy));
    evidence_.insert(QStringLiteral("workflowKind"), isEditorialCover()
        ? QStringLiteral("editorial-cover") : QStringLiteral("cutout-thumbnail"));
    evidence_.insert(QStringLiteral("expectedTopToBottomRoles"), QJsonArray::fromStringList(options_.top_to_bottom_roles));
    evidence_.insert(QStringLiteral("sourceSha256"), sha256(readFile(source)));
    if (evidence_.value(QStringLiteral("sourceSha256")).toString().isEmpty())
      return QStringLiteral("Could not hash the private imported source photo.");
    return {};
  }

  [[nodiscard]] QVariantMap installedLocalModel(const QString& model_id) const {
    for (const auto& value : edit_->availableModels()) {
      const auto model = value.toMap();
      if (model.value(QStringLiteral("id")).toString() != model_id) continue;
      const auto backend = model.value(QStringLiteral("backend")).toString().trimmed().toLower();
      const auto runtime = model.value(QStringLiteral("runtime")).toString().trimmed().toLower();
      const auto endpoint = QUrl(model.value(QStringLiteral("endpoint")).toString().trimmed());
      const auto endpoint_host = endpoint.host().toLower();
      const bool loopback = endpoint_host == QStringLiteral("localhost") ||
          endpoint_host == QStringLiteral("127.0.0.1") || endpoint_host == QStringLiteral("::1");
      if (backend != QStringLiteral("ollama") || runtime == QStringLiteral("managed") ||
          model.value(QStringLiteral("model")).toString().trimmed().isEmpty() ||
          !loopback || endpoint.port() <= 0)
        return {};
      return model;
    }
    return {};
  }

  [[nodiscard]] QByteArray readFile(const QString& path) const {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
  }

  [[nodiscard]] bool writeBytes(const QString& path, const QByteArray& bytes) const {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return false;
    return file.commit();
  }

  [[nodiscard]] bool writeCheckpoint(const QString& checkpoint) {
    if (finished_ && checkpoint != QStringLiteral("failed") && checkpoint != QStringLiteral("complete")) return false;
    evidence_.insert(QStringLiteral("lastCheckpoint"), checkpoint);
    evidence_.insert(QStringLiteral("sessionStage"), session_ ? session_->stage() : QString());
    evidence_.insert(QStringLiteral("taskTransaction"), QJsonObject::fromVariantMap(edit_ ? edit_->taskTransaction() : QVariantMap{}));
    const auto bytes = QJsonDocument(evidence_).toJson(QJsonDocument::Indented);
    return writeBytes(receipt_path_, bytes);
  }

  [[nodiscard]] QObject* qmlObject(const QString& name) const {
    if (!window_) return nullptr;
    const auto matches = window_->findChildren<QObject*>(name);
    for (auto* match : matches) {
      if (auto* item = qobject_cast<QQuickItem*>(match)) {
        if (item->isVisible()) return match;
        continue;
      }
      if (match->metaObject()->indexOfProperty("visible") >= 0 &&
          match->property("visible").toBool()) return match;
    }
    return nullptr;
  }

  [[nodiscard]] bool click(const QString& object_name) {
    auto* button = qmlObject(object_name);
    return button && button->property("visible").toBool() &&
        button->property("enabled").toBool() &&
        QMetaObject::invokeMethod(button, "clicked", Qt::DirectConnection);
  }

  void appendCaptureDiagnostic(
      const QString& name,
      const QString& status,
      const QString& reason,
      const QJsonObject& context = {}) {
    QJsonObject diagnostic{
        {QStringLiteral("stage"), name},
        {QStringLiteral("status"), status},
        {QStringLiteral("reason"), reason},
        {QStringLiteral("windowPresent"), window_ != nullptr},
        {QStringLiteral("windowVisible"), window_ && window_->isVisible()},
        {QStringLiteral("windowExposed"), window_ && window_->isExposed()},
        {QStringLiteral("windowActive"), window_ && window_->isActive()},
        {QStringLiteral("windowVisibility"), window_ ? static_cast<int>(window_->visibility()) : -1},
        {QStringLiteral("windowWidth"), window_ ? window_->width() : 0},
        {QStringLiteral("windowHeight"), window_ ? window_->height() : 0},
        {QStringLiteral("presentedFrameSerial"), static_cast<qint64>(presented_frame_serial_)},
    };
    if (auto* dialog = qmlObject(QStringLiteral("agentRunDialog"))) {
      diagnostic.insert(QStringLiteral("dialogPresentationProgress"),
          dialog->property("presentationProgress").toDouble());
    }
    if (auto* terminal = qmlObject(QStringLiteral("agentSessionTerminal")))
      diagnostic.insert(QStringLiteral("terminalStage"), terminal->property("stage").toString());
    for (auto it = context.begin(); it != context.end(); ++it) diagnostic.insert(it.key(), it.value());
    auto diagnostics = evidence_.value(QStringLiteral("screenshotCaptureDiagnostics")).toArray();
    diagnostics.append(diagnostic);
    evidence_.insert(QStringLiteral("screenshotCaptureDiagnostics"), diagnostics);
  }

  [[nodiscard]] bool capture(
      const QString& name,
      const QString& purpose = QStringLiteral("immediate"),
      const QJsonObject& context = {}) {
    last_capture_failure_detail_.clear();
    const auto fail_capture = [&](const QString& reason) {
      last_capture_failure_detail_ = reason;
      appendCaptureDiagnostic(name, QStringLiteral("failed"), reason,
          QJsonObject{{QStringLiteral("purpose"), purpose}, {QStringLiteral("captureContext"), context}});
      return false;
    };
    if (!window_) return fail_capture(QStringLiteral("window-missing"));
    if (!window_->isVisible()) return fail_capture(QStringLiteral("window-not-visible"));
    if (!window_->isExposed()) return fail_capture(QStringLiteral("window-not-exposed"));
    const auto image = window_->grabWindow();
    const auto path = QDir(screenshot_root_).filePath(name + QStringLiteral(".png"));
    if (image.isNull()) return fail_capture(QStringLiteral("grab-window-returned-null"));
    if (!image.save(path))
      return fail_capture(QStringLiteral("image-save-failed:%1").arg(path));
    auto paths = evidence_.value(QStringLiteral("screenshots")).toArray();
    paths.append(QJsonObject{{QStringLiteral("stage"), name}, {QStringLiteral("path"), path},
        {QStringLiteral("sha256"), sha256(readFile(path))}});
    evidence_.insert(QStringLiteral("screenshots"), paths);
    appendCaptureDiagnostic(name, QStringLiteral("captured"), QStringLiteral("presented-image-saved"),
        QJsonObject{{QStringLiteral("purpose"), purpose}, {QStringLiteral("path"), path},
            {QStringLiteral("captureContext"), context}});
    if (!writeCheckpoint(QStringLiteral("screenshot_") + name))
      return fail_capture(QStringLiteral("receipt-checkpoint-write-failed"));
    return true;
  }

  [[nodiscard]] bool hasScreenshot(const QString& name) const {
    const auto screenshots = evidence_.value(QStringLiteral("screenshots")).toArray();
    return std::any_of(screenshots.cbegin(), screenshots.cend(), [&](const QJsonValue& entry) {
      return entry.toObject().value(QStringLiteral("stage")).toString() == name;
    });
  }

  [[nodiscard]] QString presentedCaptureBlocker(PresentedCapturePurpose purpose) const {
    if (!window_) return QStringLiteral("window-missing");
    if (!window_->isVisible()) return QStringLiteral("window-not-visible");
    if (!window_->isExposed()) return QStringLiteral("window-not-exposed");
    if (purpose == PresentedCapturePurpose::Workspace) return {};
    auto* dialog = qmlObject(QStringLiteral("agentRunDialog"));
    if (!dialog) return QStringLiteral("agent-dialog-not-visible");
    const auto presentation_progress = dialog->property("presentationProgress").toDouble();
    if (!std::isfinite(presentation_progress) || presentation_progress < 0.999)
      return QStringLiteral("dialog-presentation-in-progress:%1").arg(presentation_progress, 0, 'f', 3);
    auto* terminal = qmlObject(QStringLiteral("agentSessionTerminal"));
    if (!terminal) return QStringLiteral("session-terminal-not-visible");
    if (purpose == PresentedCapturePurpose::Request) {
      if (dialog->property("terminalPrompt").toString() != options_.initial_prompt)
        return QStringLiteral("requested-prompt-not-bound-to-visible-terminal");
      auto* submit = qmlObject(QStringLiteral("agentSessionSubmitButton"));
      if (!submit) return QStringLiteral("request-submit-control-not-visible");
      if (!submit->property("enabled").toBool()) return QStringLiteral("request-submit-control-not-enabled");
    } else if (purpose == PresentedCapturePurpose::PlanningCancelled) {
      if (terminal->property("stage").toString() != QStringLiteral("interrupted"))
        return QStringLiteral("terminal-not-interrupted");
      auto* retry = qmlObject(QStringLiteral("agentSessionRetryButton"));
      if (!retry || !retry->property("enabled").toBool())
        return QStringLiteral("planning-retry-control-not-ready");
    } else if (purpose == PresentedCapturePurpose::Plan) {
      const auto stage = terminal->property("stage").toString();
      if (stage != QStringLiteral("plan-ready"))
        return QStringLiteral("terminal-not-plan-ready:%1").arg(stage);
      auto* start = qmlObject(QStringLiteral("agentSessionStartButton"));
      if (!start) return QStringLiteral("plan-start-control-not-visible");
      if (!start->property("enabled").toBool()) return QStringLiteral("plan-start-control-not-enabled");
    } else if (purpose == PresentedCapturePurpose::Review) {
      const auto stage = terminal->property("stage").toString();
      if (stage != QStringLiteral("review"))
        return QStringLiteral("terminal-not-review:%1").arg(stage);
      auto* revert = qmlObject(QStringLiteral("agentSessionReviewRevertButton"));
      if (!revert) return QStringLiteral("review-revert-control-not-visible");
      if (!revert->property("enabled").toBool())
        return QStringLiteral("review-revert-control-not-enabled");
    } else {
      if (terminal->property("stage").toString() != QStringLiteral("interrupted"))
        return QStringLiteral("revert-terminal-not-interrupted");
      const auto transaction = edit_ ? edit_->taskTransaction() : QVariantMap{};
      if (transaction.value(QStringLiteral("status")).toString() != QStringLiteral("reverted"))
        return QStringLiteral("task-transaction-not-reverted");
      if (transaction.value(QStringLiteral("taskId")).toString() != task_id_ || task_id_.isEmpty())
        return QStringLiteral("reverted-task-identity-mismatch");
    }
    return {};
  }

  [[nodiscard]] static QString presentedCapturePurposeName(PresentedCapturePurpose purpose) {
    switch (purpose) {
      case PresentedCapturePurpose::Workspace: return QStringLiteral("workspace");
      case PresentedCapturePurpose::Request: return QStringLiteral("request");
      case PresentedCapturePurpose::PlanningCancelled: return QStringLiteral("planning-cancelled");
      case PresentedCapturePurpose::Plan: return QStringLiteral("plan");
      case PresentedCapturePurpose::Review: return QStringLiteral("review");
      case PresentedCapturePurpose::WholeTaskReverted: return QStringLiteral("whole-task-reverted");
    }
    return {};
  }

  [[nodiscard]] PresentedCaptureResult captureAfterPresentation(
      const QString& name,
      PresentedCapturePurpose purpose) {
    last_capture_failure_detail_.clear();
    if (hasScreenshot(name)) {
      pending_capture_ = {};
      return PresentedCaptureResult::Captured;
    }
    constexpr qint64 kSettledPresentationMs = 420;
    constexpr qint64 kCaptureTimeoutMs = 6000;
    const auto now_ms = QDateTime::currentMSecsSinceEpoch();
    if (!pending_capture_.active) {
      pending_capture_.active = true;
      pending_capture_.name = name;
      pending_capture_.purpose = purpose;
      pending_capture_.started_at_ms = now_ms;
      pending_capture_.minimum_capture_at_ms = now_ms + kSettledPresentationMs;
      pending_capture_.last_blocker = QStringLiteral("waiting-for-settled-presentation");
      // This opt-in proof owns its private app window. Bring that window to
      // the foreground before testing actual exposure and presented frames.
      if (window_) {
        pending_capture_.window_activation_before = {
            {QStringLiteral("visible"), window_->isVisible()},
            {QStringLiteral("exposed"), window_->isExposed()},
            {QStringLiteral("active"), window_->isActive()},
            {QStringLiteral("visibility"), static_cast<int>(window_->visibility())},
        };
        if (window_->visibility() == QWindow::Minimized) window_->showNormal();
        window_->raise();
        window_->requestActivate();
        pending_capture_.window_activation_requested = true;
      }
      return PresentedCaptureResult::Pending;
    }
    if (pending_capture_.name != name || pending_capture_.purpose != purpose) {
      last_capture_failure_detail_ = QStringLiteral("another-presented-capture-is-pending:%1")
                                         .arg(pending_capture_.name);
      return PresentedCaptureResult::Failed;
    }

    const auto elapsed_ms = now_ms - pending_capture_.started_at_ms;
    const auto blocker = presentedCaptureBlocker(purpose);
    if (!blocker.isEmpty()) {
      pending_capture_.last_blocker = blocker;
      pending_capture_.frame_request_sent = false;
    } else if (now_ms >= pending_capture_.minimum_capture_at_ms) {
      if (!pending_capture_.frame_request_sent) {
        pending_capture_.frame_request_sent = true;
        pending_capture_.frame_serial_before_request = presented_frame_serial_;
        window_->update();
        pending_capture_.last_blocker = QStringLiteral("waiting-for-frame-swapped-after-settled-state");
      } else if (presented_frame_serial_ > pending_capture_.frame_serial_before_request) {
        const auto capture_purpose = presentedCapturePurposeName(purpose);
        const QJsonObject context{
            {QStringLiteral("purpose"), capture_purpose},
            {QStringLiteral("settleWaitMs"), elapsed_ms},
            {QStringLiteral("minimumSettleWaitMs"), kSettledPresentationMs},
            {QStringLiteral("frameSwapsSinceRequest"),
                static_cast<qint64>(presented_frame_serial_ - pending_capture_.frame_serial_before_request)},
            {QStringLiteral("frameRequestAfterSettledState"), true},
            {QStringLiteral("windowActivationRequested"), pending_capture_.window_activation_requested},
            {QStringLiteral("windowActivationBefore"), pending_capture_.window_activation_before},
        };
        auto completed_context = context;
        if (purpose == PresentedCapturePurpose::Review) {
          auto* revert = qmlObject(QStringLiteral("agentSessionReviewRevertButton"));
          completed_context.insert(QStringLiteral("reviewRevertControlVisible"), revert != nullptr);
          completed_context.insert(QStringLiteral("reviewRevertControlEnabled"),
              revert && revert->property("enabled").toBool());
        } else if (purpose == PresentedCapturePurpose::WholeTaskReverted) {
          const auto transaction = edit_->taskTransaction();
          const auto restored_preview = preview_->currentAssetBoundAdjustmentPatchJson().toUtf8();
          const auto restored_catalog = catalog_->agentTaskRecipeResourceAdapter(options_.asset_id, task_id_).read();
          completed_context.insert(QStringLiteral("taskId"), task_id_);
          completed_context.insert(QStringLiteral("assetId"), options_.asset_id);
          completed_context.insert(QStringLiteral("taskTransaction"), QJsonObject::fromVariantMap(transaction));
          completed_context.insert(QStringLiteral("taskTransactionStatus"),
              transaction.value(QStringLiteral("status")).toString());
          completed_context.insert(QStringLiteral("taskTransactionTaskId"),
              transaction.value(QStringLiteral("taskId")).toString());
          completed_context.insert(QStringLiteral("baselinePreviewRecipeSha256"), sha256(baseline_preview_));
          completed_context.insert(QStringLiteral("baselineCatalogRecipeSha256"), sha256(baseline_catalog_));
          completed_context.insert(QStringLiteral("restoredPreviewRecipeSha256"), sha256(restored_preview));
          completed_context.insert(QStringLiteral("restoredCatalogRecipeSha256"), sha256(restored_catalog));
        }
        pending_capture_ = {};
        if (!capture(name, QStringLiteral("presented-frame"), completed_context))
          return PresentedCaptureResult::Failed;
        return PresentedCaptureResult::Captured;
      }
    }

    if (elapsed_ms >= kCaptureTimeoutMs) {
      const auto reason = QStringLiteral("presented-capture-timeout:last-blocker=%1;elapsed-ms=%2;frame-serial=%3")
          .arg(pending_capture_.last_blocker).arg(elapsed_ms).arg(presented_frame_serial_);
      const QJsonObject context{
          {QStringLiteral("purpose"), presentedCapturePurposeName(purpose)},
          {QStringLiteral("elapsedMs"), elapsed_ms},
          {QStringLiteral("settledWaitMs"), kSettledPresentationMs},
          {QStringLiteral("frameRequestSent"), pending_capture_.frame_request_sent},
          {QStringLiteral("windowActivationRequested"), pending_capture_.window_activation_requested},
          {QStringLiteral("windowActivationBefore"), pending_capture_.window_activation_before},
      };
      appendCaptureDiagnostic(name, QStringLiteral("failed"), reason, context);
      last_capture_failure_detail_ = reason;
      pending_capture_ = {};
      return PresentedCaptureResult::Failed;
    }
    return PresentedCaptureResult::Pending;
  }

  [[nodiscard]] bool hasPendingPresentedCapture() const {
    return pending_capture_.active;
  }

  [[nodiscard]] bool beginExport(const QString& name) {
    export_path_ = QDir(output_root_).filePath(name + QStringLiteral(".png"));
    QFile::remove(export_path_);
    export_name_ = name;
    if (!preview_->startFlattenedPhotoExport(export_path_)) return false;
    return true;
  }

  [[nodiscard]] bool exportSettled() const {
    return !preview_->photoExportBusy();
  }

  [[nodiscard]] bool readExport(QImage* image, QString* error) const {
    if (preview_->photoExportBusy()) return false;
    if (!preview_->photoExportError().isEmpty()) {
      *error = preview_->photoExportError();
      return false;
    }
    *image = QImage(export_path_);
    if (image->isNull()) {
      *error = QStringLiteral("The native full-resolution export did not produce a readable image.");
      return false;
    }
    return true;
  }

  void beginBaselineExport() {
    if (preview_->recipeSavePending() ||
        !catalog_->agentTaskRecipePersistenceSettled(options_.asset_id)) {
      fail(QStringLiteral("The initial recipe baseline was requested before Preview and Catalog persistence settled."));
      return;
    }
    if (!preview_->recipeSaveError().isEmpty()) {
      fail(QStringLiteral("The initial Preview recipe save failed: %1").arg(preview_->recipeSaveError()));
      return;
    }
    baseline_preview_ = preview_->currentAssetBoundAdjustmentPatchJson().toUtf8();
    baseline_catalog_ = catalog_->agentTaskRecipeResourceAdapter(options_.asset_id).read();
    if (baseline_preview_.isEmpty() || baseline_catalog_.isEmpty()) {
      fail(QStringLiteral("Could not capture the exact initial Preview and Catalog recipe baselines."));
      return;
    }
    evidence_.insert(QStringLiteral("baselinePreviewSha256"), sha256(baseline_preview_));
    evidence_.insert(QStringLiteral("baselineCatalogSha256"), sha256(baseline_catalog_));
    if (!writeBytes(QDir(output_root_).filePath(QStringLiteral("baseline-recipe.json")), baseline_preview_) ||
        !writeBytes(QDir(output_root_).filePath(QStringLiteral("baseline-catalog-resource.json")), baseline_catalog_)) {
      fail(QStringLiteral("Could not retain the exact initial Preview and Catalog baselines."));
      return;
    }
    const auto catalog_baseline = QJsonDocument::fromJson(baseline_catalog_);
    const auto preview_baseline = QJsonDocument::fromJson(baseline_preview_);
    const auto catalog_recipe = catalog_baseline.object().value(QStringLiteral("legacy_recipe"));
    // Preview wraps AdjustmentState in a patch; Catalog adds the legacy recipe
    // version. Compare the exact state, retaining both complete restore bytes.
    auto persisted_adjustment_state = catalog_recipe.toObject();
    const auto persisted_recipe_version = persisted_adjustment_state.take(QStringLiteral("recipeVersion"));
    const auto preview_recipe = preview_baseline.object().value(QStringLiteral("adjustmentState"));
    if (!catalog_baseline.isObject() || !preview_baseline.isObject() ||
        catalog_baseline.object().value(QStringLiteral("asset_id")).toString() != options_.asset_id ||
        preview_baseline.object().value(QStringLiteral("format")).toString() != QStringLiteral("photo-editor-adjustment-patch") ||
        preview_baseline.object().value(QStringLiteral("scope")).toString() != QStringLiteral("asset-bound") ||
        !catalog_recipe.isObject() || !preview_recipe.isObject() ||
        persisted_recipe_version.toInt(-1) != 12 ||
        persisted_adjustment_state != preview_recipe.toObject()) {
      fail(QStringLiteral("The settled Preview recipe does not match the persisted recipe inside the Catalog resource baseline."));
      return;
    }
    evidence_.insert(QStringLiteral("baselinePreviewMatchesCatalogRecipe"), true);
    const auto baseline = parse_adjustment_patch_json(QString::fromUtf8(baseline_preview_));
    if (!baseline.ok() || !baseline.state.has_value()) {
      fail(QStringLiteral("Could not parse the exact baseline recipe for later color and crop comparison."));
      return;
    }
    baseline_state_ = *baseline.state;
    baseline_composition_ = preview_->photoCompositionCommand({
        {QStringLiteral("assetId"), options_.asset_id},
        {QStringLiteral("operation"), QStringLiteral("inspect")}});
    if (!baseline_composition_.value(QStringLiteral("ok")).toBool()) {
      fail(QStringLiteral("Could not inspect the actual baseline composition."));
      return;
    }
    for (const auto& layer_value : baseline_composition_.value(QStringLiteral("layers")).toList()) {
      const auto id = layer_value.toMap().value(QStringLiteral("id")).toString();
      if (!id.isEmpty()) baseline_layer_ids_.insert(id);
    }
    for (const auto& mask_value : baseline_composition_.value(QStringLiteral("masks")).toList()) {
      const auto id = mask_value.toMap().value(QStringLiteral("id")).toString();
      if (!id.isEmpty()) baseline_mask_ids_.insert(id);
    }
    evidence_.insert(QStringLiteral("baselineComposition"), QJsonObject::fromVariantMap(baseline_composition_));
    if (!beginExport(QStringLiteral("baseline"))) {
      fail(QStringLiteral("The native baseline export could not start."));
      return;
    }
    phase_ = Phase::BaselineExport;
    static_cast<void>(writeCheckpoint(QStringLiteral("baseline_export_started")));
  }

  [[nodiscard]] bool validatePlan(const QString& kind) {
    if (session_->stage() != QStringLiteral("plan-ready") || !session_->canStart()) return false;
    const auto plan = session_->plan();
    const auto target_ids = plan.value(QStringLiteral("targetIds")).toStringList();
    if (target_ids != QStringList{options_.asset_id}) {
      fail(QStringLiteral("The live plan changed the explicitly pinned imported photo target."));
      return false;
    }
    const auto expected_copy = kind == QStringLiteral("revision")
        ? options_.revision_exact_copy : options_.exact_copy;
    const auto expected_texts = expectedCoverCopy(kind);
    const auto planned_copy = plan.value(QStringLiteral("exactCopy")).toList();
    const auto contains_copy = [&](const QString& text) {
      return std::any_of(planned_copy.cbegin(), planned_copy.cend(), [&](const QVariant& value) {
            return value.toMap().value(QStringLiteral("text")).toString() == text;
          });
    };
    const bool exact_copy_present = contains_copy(expected_copy) &&
        std::all_of(expected_texts.cbegin(), expected_texts.cend(), contains_copy);
    if (!exact_copy_present) {
      fail(QStringLiteral("The live approved plan did not preserve the required exact copy."));
      return false;
    }
    const auto key = kind + QStringLiteral("Plan");
    evidence_.insert(key, QJsonObject::fromVariantMap(plan));
    const auto capture_result = captureAfterPresentation(
        kind + QStringLiteral("-plan"), PresentedCapturePurpose::Plan);
    if (capture_result == PresentedCaptureResult::Pending) return false;
    if (capture_result == PresentedCaptureResult::Failed) {
      fail(QStringLiteral("The visible native plan screen could not be captured: %1")
               .arg(last_capture_failure_detail_));
      return false;
    }
    return true;
  }

  [[nodiscard]] QString taskId() const {
    auto id = edit_->taskTransaction().value(QStringLiteral("taskId")).toString();
    if (id.isEmpty()) id = edit_->lastRunReport().value(QStringLiteral("taskId")).toString();
    return id;
  }

  [[nodiscard]] bool validateReview(const QString& label, bool composition_required) {
    const auto transaction = edit_->taskTransaction();
    const auto report = edit_->lastRunReport();
    if (session_->stage() != QStringLiteral("review") || !edit_->draftActive() ||
        transaction.value(QStringLiteral("status")).toString() != QStringLiteral("review") ||
        !report.value(QStringLiteral("ok")).toBool()) {
      fail(QStringLiteral("The actual AgentEdit run did not settle to a reversible Review state: %1").arg(label));
      return false;
    }
    const auto current_task_id = taskId();
    if (current_task_id.isEmpty() || (!task_id_.isEmpty() && task_id_ != current_task_id)) {
      fail(QStringLiteral("The task identity changed during the approved revision."));
      return false;
    }
    task_id_ = current_task_id;
    if (report.value(QStringLiteral("modelId")).toString() != options_.installed_model_id) {
      fail(QStringLiteral("The completed run report does not identify the explicitly selected installed local model."));
      return false;
    }
    if (!options_.cover_copy.isEmpty()) {
      const auto audit = report.value(QStringLiteral("modelPromptAudit")).toMap();
      const auto completion = report.value(QStringLiteral("nativeCompositionCompletion")).toMap();
      const auto verified_model_completion =
          audit.value(QStringLiteral("modelDecision")).toString() == QStringLiteral("edit_intent") &&
          audit.value(QStringLiteral("completionMode")).toString() ==
              QStringLiteral("preserve-after-native-composition-readback") &&
          completion.value(QStringLiteral("requested")).toBool() &&
          completion.value(QStringLiteral("controllerVerified")).toBool() &&
          completion.value(QStringLiteral("verificationContract")).toString() ==
              QStringLiteral("agent-native-composition-completion-controller-v1") &&
          completion.value(QStringLiteral("assetId")).toString() == options_.asset_id &&
          completion.value(QStringLiteral("acceptedMutationCount")).toInt() > 0 &&
          completion.value(QStringLiteral("readbackConfirmed")).toBool();
      if (!verified_model_completion) {
        fail(QStringLiteral("The cover run did not retain a verified model completion after fresh native readback: %1")
                 .arg(label));
        return false;
      }
    }
    evidence_.insert(QStringLiteral("taskId"), task_id_);
    evidence_.insert(label + QStringLiteral("RunReport"), QJsonObject::fromVariantMap(report));
    evidence_.insert(label + QStringLiteral("TaskTransaction"), QJsonObject::fromVariantMap(transaction));
    if (composition_required && !validateComposition(label)) return false;
    const auto capture_result = captureAfterPresentation(
        label + QStringLiteral("-review"), PresentedCapturePurpose::Review);
    if (capture_result == PresentedCaptureResult::Pending) return false;
    if (capture_result == PresentedCaptureResult::Failed) {
      fail(QStringLiteral("The visible native Review screen could not be captured: %1")
               .arg(last_capture_failure_detail_));
      return false;
    }
    return true;
  }

  [[nodiscard]] QVariantList runToolReports(const QVariantMap& report) const {
    const auto model_plan = report.value(QStringLiteral("modelToolPlan")).toMap();
    auto reports = model_plan.value(QStringLiteral("toolReports")).toList();
    if (reports.isEmpty()) reports = report.value(QStringLiteral("toolReports")).toList();
    return reports;
  }

  [[nodiscard]] QVariantMap successfulCompositionReceipt(
      const QVariantMap& report, const QString& operation,
      const QString& exact_text = {}) const {
    for (const auto& receipt_value : runToolReports(report)) {
      const auto receipt = receipt_value.toMap();
      const auto arguments = receipt.value(QStringLiteral("arguments")).toMap();
      if (!receipt.value(QStringLiteral("ok")).toBool()) continue;
      const auto tool_name = receipt.value(QStringLiteral("toolName")).toString();
      const auto values = arguments.value(QStringLiteral("values")).toMap();
      if (tool_name == QStringLiteral("photo.edit_composition") &&
          arguments.value(QStringLiteral("operation")).toString() == operation &&
          (exact_text.isEmpty() || values.value(QStringLiteral("text")).toString() == exact_text))
        return receipt;

      // The model can create editable text through either the focused
      // composition tool or the discovered native command adapter. Bind this
      // fallback to the exact requested copy and the ID actually returned by
      // the successful native command; the final document inspection still
      // proves that ID is an editable text layer.
      if (operation == QStringLiteral("add_text") &&
          tool_name == QStringLiteral("app.invoke_command") &&
          !exact_text.isEmpty()) {
        const auto content = receipt.value(QStringLiteral("structuredContent")).toMap();
        if (values.value(QStringLiteral("text")).toString() == exact_text &&
            content.value(QStringLiteral("changed")).toBool() &&
            !content.value(QStringLiteral("createdLayerId")).toString().isEmpty())
          return receipt;
      }
    }
    return {};
  }

  [[nodiscard]] bool hasActualColorChange(
      const EditRecipeState& before, const EditRecipeState& after, QStringList* changed_fields) const {
    const auto compare = [&](const QString& name, double lhs, double rhs) {
      if (std::abs(lhs - rhs) > 1e-5) changed_fields->append(name);
    };
    compare(QStringLiteral("exposure"), before.exposure, after.exposure);
    compare(QStringLiteral("contrast"), before.contrast, after.contrast);
    compare(QStringLiteral("highlights"), before.highlights, after.highlights);
    compare(QStringLiteral("shadows"), before.shadows, after.shadows);
    compare(QStringLiteral("whites"), before.whites, after.whites);
    compare(QStringLiteral("blacks"), before.blacks, after.blacks);
    compare(QStringLiteral("brightness"), before.brightness, after.brightness);
    compare(QStringLiteral("temperature"), before.temperature, after.temperature);
    compare(QStringLiteral("tint"), before.tint, after.tint);
    compare(QStringLiteral("vibrance"), before.vibrance, after.vibrance);
    compare(QStringLiteral("saturation"), before.saturation, after.saturation);
    compare(QStringLiteral("texture"), before.texture, after.texture);
    compare(QStringLiteral("clarity"), before.clarity, after.clarity);
    compare(QStringLiteral("dehaze"), before.dehaze, after.dehaze);
    compare(QStringLiteral("sharpening"), before.sharpening, after.sharpening);
    if (before.master_curve_points != after.master_curve_points) changed_fields->append(QStringLiteral("masterCurve"));
    if (before.luma_curve_points != after.luma_curve_points) changed_fields->append(QStringLiteral("lumaCurve"));
    if (before.red_curve_points != after.red_curve_points ||
        before.green_curve_points != after.green_curve_points ||
        before.blue_curve_points != after.blue_curve_points) changed_fields->append(QStringLiteral("rgbCurves"));
    compare(QStringLiteral("gradingShadowsSaturation"), before.grading_shadows_saturation, after.grading_shadows_saturation);
    compare(QStringLiteral("gradingMidtonesSaturation"), before.grading_midtones_saturation, after.grading_midtones_saturation);
    compare(QStringLiteral("gradingHighlightsSaturation"), before.grading_highlights_saturation, after.grading_highlights_saturation);
    if (!changed_fields->isEmpty()) return true;

    QHash<QString, QVariantMap> before_layers;
    for (const auto& value : before.layers) {
      const auto layer = value.toMap();
      before_layers.insert(layer.value(QStringLiteral("id")).toString(), layer);
    }
    static const QStringList color_keys{
        QStringLiteral("exposure"), QStringLiteral("contrast"), QStringLiteral("highlights"),
        QStringLiteral("shadows"), QStringLiteral("whites"), QStringLiteral("blacks"),
        QStringLiteral("brightness"), QStringLiteral("temperature"), QStringLiteral("tint"),
        QStringLiteral("vibrance"), QStringLiteral("saturation"), QStringLiteral("texture"),
        QStringLiteral("clarity"), QStringLiteral("dehaze"), QStringLiteral("curvePoints"),
        QStringLiteral("masterCurvePoints"), QStringLiteral("rgbCurves")};
    for (const auto& value : after.layers) {
      const auto layer = value.toMap();
      if (layer.value(QStringLiteral("kind")).toString() != QStringLiteral("adjustment")) continue;
      const auto id = layer.value(QStringLiteral("id")).toString();
      const auto prior = before_layers.value(id);
      for (const auto& key : color_keys) {
        if (layer.value(key) != prior.value(key) && layer.value(key).isValid()) {
          changed_fields->append(QStringLiteral("layer:%1:%2").arg(id, key));
          break;
        }
      }
    }
    return !changed_fields->isEmpty();
  }

  [[nodiscard]] bool validateEditorialCoverComposition(
      const QString& label, const QVariantMap& inspect,
      const QVariantList& layers, const QHash<QString, QVariantMap>& recipe_layers,
      const EditRecipeState& document) {
    const auto expected_texts = expectedCoverCopy(label);
    if (expected_texts.size() != 3 || options_.top_to_bottom_roles != QStringList{QStringLiteral("text")}) {
      fail(QStringLiteral("The editorial cover proof requires exactly three ordered copy lines and a text-only stack contract."));
      return false;
    }
    QStringList changed_photo_fields;
    if (hasActualColorChange(baseline_state_, document, &changed_photo_fields)) {
      fail(QStringLiteral("The editorial cover changed photo color fields instead of preserving the original image."));
      return false;
    }
    evidence_.insert(QStringLiteral("photoColorRecipePreserved"), true);
    const auto initial_report = label == QStringLiteral("initial")
        ? edit_->lastRunReport() : initial_run_report_;
    QHash<QString, QString> layer_ids;
    QSet<QString> unique_layer_ids;
    for (const auto& text : expected_texts) {
      const auto receipt = successfulCompositionReceipt(
          initial_report, QStringLiteral("add_text"), text);
      const auto content = receipt.value(QStringLiteral("structuredContent")).toMap();
      const auto id = content.value(QStringLiteral("createdLayerId")).toString();
      if (receipt.isEmpty() || !content.value(QStringLiteral("changed")).toBool() || id.isEmpty() ||
          baseline_layer_ids_.contains(id) || unique_layer_ids.contains(id)) {
        fail(QStringLiteral("The production run lacks a successful exact-copy text creation receipt with a new stable ID: %1").arg(text));
        return false;
      }
      unique_layer_ids.insert(id);
      layer_ids.insert(text, id);
    }
    if (label == QStringLiteral("initial")) initial_run_report_ = initial_report;

    QVariantList verified_texts;
    QHash<QString, QVariantMap> visible_layers;
    int current_text_layer_count = 0;
    for (const auto& layer_value : layers) {
      const auto layer = layer_value.toMap();
      const auto id = layer.value(QStringLiteral("id")).toString();
      if (!baseline_layer_ids_.contains(id) && !unique_layer_ids.contains(id)) {
        fail(QStringLiteral("The cover task added an unapproved editable layer: %1").arg(layer.value(QStringLiteral("name")).toString()));
        return false;
      }
      if (layer.value(QStringLiteral("kind")).toString() != QStringLiteral("text")) continue;
      ++current_text_layer_count;
      const auto copy = layer.value(QStringLiteral("text")).toString();
      if (expected_texts.contains(copy)) {
        if (visible_layers.contains(copy)) {
          fail(QStringLiteral("The final cover contains duplicate editable copy: %1").arg(copy));
          return false;
        }
        visible_layers.insert(copy, layer);
      }
    }
    if (current_text_layer_count != expected_texts.size() || visible_layers.size() != expected_texts.size()) {
      fail(QStringLiteral("The native cover must contain exactly the three approved editable text layers."));
      return false;
    }

    const QSize canvas_size(inspect.value(QStringLiteral("width")).toInt(),
        inspect.value(QStringLiteral("height")).toInt());
    if (canvas_size.isEmpty()) {
      fail(QStringLiteral("The native cover inspection did not provide valid canvas dimensions."));
      return false;
    }
    const int safe_margin = qMax(1, qRound(qMin(canvas_size.width(), canvas_size.height()) * 0.025));
    const auto style_size = [&](const QVariantMap& layer) {
      const auto style = recipe_layers.value(layer.value(QStringLiteral("id")).toString())
          .value(QStringLiteral("textTypography")).toMap();
      return style.value(QStringLiteral("size")).toDouble();
    };
    QList<QRect> bounds;
    QList<double> sizes;
    QList<int> x_positions;
    QList<int> y_positions;
    cover_text_bounds_.clear();
    cover_text_layers_.clear();
    for (int index = 0; index < expected_texts.size(); ++index) {
      const auto& text = expected_texts.at(index);
      const auto layer = visible_layers.value(text);
      cover_text_layers_.append(layer);
      const auto id = layer.value(QStringLiteral("id")).toString();
      const auto expected_id = layer_ids.value(text);
      if (id.isEmpty() || id != expected_id || !layer.value(QStringLiteral("enabled"), true).toBool()) {
        fail(QStringLiteral("The exact-copy layer does not match its returned creation ID or is disabled: %1").arg(text));
        return false;
      }
      const int x = layer.value(QStringLiteral("x")).toInt();
      const int y = layer.value(QStringLiteral("y")).toInt();
      bool scale_ok = true;
      const auto unbounded_scale = layer.contains(QStringLiteral("scale"))
          ? layer.value(QStringLiteral("scale")).toDouble(&scale_ok) : 1.0;
      const bool valid_scale = scale_ok && std::isfinite(unbounded_scale) && unbounded_scale > 0.0;
      const auto layer_scale = valid_scale ? qBound(0.05, unbounded_scale, 32.0) : 0.0;
      const int width = qRound(layer.value(QStringLiteral("pixelWidth")).toDouble() *
          layer_scale);
      const int height = qRound(layer.value(QStringLiteral("pixelHeight")).toDouble() *
          layer_scale);
      const QRect text_bounds(x, y, width, height);
      const double size = style_size(layer);
      const double effective_size = size * layer_scale;
      if (text_bounds.isEmpty() || !QRect(QPoint(), canvas_size).contains(text_bounds) ||
          text_bounds.left() < safe_margin || text_bounds.top() < safe_margin ||
          text_bounds.right() >= canvas_size.width() - safe_margin ||
          text_bounds.bottom() >= canvas_size.height() - safe_margin ||
          std::abs(layer.value(QStringLiteral("rotation")).toDouble()) > 1.0 ||
          !valid_scale || !std::isfinite(size) || size <= 0.0 ||
          !std::isfinite(effective_size) || effective_size <= 0.0) {
        fail(QStringLiteral("The cover copy is clipped, lacks a safe margin, has unsupported rotation, or lacks explicit typography: %1").arg(text));
        return false;
      }
      for (const auto& earlier : bounds) {
        if (text_bounds.intersects(earlier)) {
          fail(QStringLiteral("The three editable cover lines overlap: %1").arg(text));
          return false;
        }
      }
      bounds.append(text_bounds);
      cover_text_bounds_.append(text_bounds);
      sizes.append(effective_size);
      x_positions.append(x);
      y_positions.append(y);
      const auto color_value = recipe_layers.value(id).value(QStringLiteral("textTypography")).toMap()
          .value(QStringLiteral("color"));
      const auto typography = recipe_layers.value(id)
          .value(QStringLiteral("textTypography")).toMap();
      const auto font_family = typography.value(QStringLiteral("family")).toString().trimmed();
      const auto font_face = typography.value(QStringLiteral("face")).toString().trimmed();
      const auto font_weight = typography.value(QStringLiteral("weight")).toInt();
      QVariantMap font_inspection_values;
      if (!font_family.isEmpty())
        font_inspection_values.insert(QStringLiteral("family"), font_family);
      if (!font_face.isEmpty())
        font_inspection_values.insert(QStringLiteral("face"), font_face);
      const auto font_inspection = preview_->photoCompositionCommand({
          {QStringLiteral("assetId"), options_.asset_id},
          {QStringLiteral("operation"), QStringLiteral("inspect")},
          {QStringLiteral("values"), font_inspection_values}});
      const auto font_faces = font_inspection.value(QStringLiteral("requestedFamilyFaces")).toStringList();
      const auto font_axes = font_inspection.value(QStringLiteral("fontAxes")).toList();
      const auto font_face_listed = std::any_of(font_faces.cbegin(), font_faces.cend(),
          [&font_face](const QString& available) {
            return available.compare(font_face, Qt::CaseInsensitive) == 0;
          });
      const auto font_request_preserved =
          font_inspection.value(QStringLiteral("fontFamily")).toString() == font_family &&
          font_inspection.value(QStringLiteral("fontFace")).toString() == font_face;
      const auto font_family_available =
          font_inspection.value(QStringLiteral("fontFamilyAvailable")).toBool();
      const auto font_face_available =
          font_inspection.value(QStringLiteral("fontFaceAvailable")).toBool();
      const QColor color(color_value.toString());
      if (!typography.contains(QStringLiteral("family")) || font_family.isEmpty() ||
          !typography.contains(QStringLiteral("face")) || font_face.isEmpty() ||
          !typography.contains(QStringLiteral("size")) ||
          !typography.contains(QStringLiteral("weight")) || font_weight <= 0 ||
          !typography.contains(QStringLiteral("color")) || !color.isValid()) {
        fail(QStringLiteral("The cover text readback must include an explicit font family, face, size, weight and color: %1").arg(text));
        return false;
      }
      if (!font_inspection.value(QStringLiteral("ok")).toBool() ||
          !font_family_available || !font_face_available || !font_face_listed ||
          !font_request_preserved) {
        fail(QStringLiteral("The requested font family or face is unavailable; select an exact face from native fontFaces: %1").arg(text));
        return false;
      }
      if (!color.isValid() || color.red() < 210 || color.green() < 200 || color.blue() < 170 ||
          color.red() < color.blue() - 20) {
        fail(QStringLiteral("The cover text must use readable warm-white typography: %1").arg(text));
        return false;
      }
      verified_texts.append(QVariantMap{{QStringLiteral("text"), text}, {QStringLiteral("layerId"), id},
          {QStringLiteral("pixelBounds"), QVariantMap{{QStringLiteral("x"), text_bounds.x()},
              {QStringLiteral("y"), text_bounds.y()}, {QStringLiteral("width"), text_bounds.width()},
              {QStringLiteral("height"), text_bounds.height()}}},
          {QStringLiteral("typographySize"), size},
          {QStringLiteral("layerScale"), layer_scale},
          {QStringLiteral("effectiveTypographySize"), effective_size},
          {QStringLiteral("color"), color.name(QColor::HexArgb)},
          {QStringLiteral("fontFamily"), font_family}, {QStringLiteral("fontFace"), font_face},
          {QStringLiteral("fontWeight"), font_weight},
          {QStringLiteral("fontFamilyAvailable"), font_family_available},
          {QStringLiteral("fontFaces"), font_faces},
          {QStringLiteral("fontFaceAvailable"), font_face_available},
          {QStringLiteral("fontRequestPreserved"), font_request_preserved},
          {QStringLiteral("fontAxes"), font_axes},
          {QStringLiteral("emptyAxesValidForAvailableFace"), font_face_available && font_axes.isEmpty()},
          {QStringLiteral("usesSystemDefaultFont"), font_family.compare(QStringLiteral("System"), Qt::CaseInsensitive) == 0},
          {QStringLiteral("typography"), recipe_layers.value(id)
              .value(QStringLiteral("textTypography")).toMap()}});
    }
    const double required_headline_ratio = label == QStringLiteral("initial") ? 3.0 : 2.0;
    if (sizes.at(1) < required_headline_ratio * sizes.at(0) ||
        sizes.at(1) < required_headline_ratio * sizes.at(2) ||
        y_positions.at(0) >= y_positions.at(1) || y_positions.at(1) >= y_positions.at(2) ||
        std::abs(x_positions.at(0) - x_positions.at(1)) > safe_margin ||
        std::abs(x_positions.at(0) - x_positions.at(2)) > safe_margin) {
      fail(label == QStringLiteral("initial")
          ? QStringLiteral("The initial cover needs a headline at least three times the size of both the small kicker and quiet footer, with aligned top-to-bottom placement.")
          : QStringLiteral("The revised cover must keep the headline at least twice the size of both the small kicker and quiet footer, with aligned top-to-bottom placement."));
      return false;
    }
    const auto rendered_ink_report = validateRenderedWarmWhiteInk(
        document, visible_layers, recipe_layers, canvas_size);
    evidence_.insert(label + QStringLiteral("RenderedWarmWhiteInk"), QJsonObject::fromVariantMap(rendered_ink_report));
    if (!rendered_ink_report.value(QStringLiteral("passed")).toBool()) {
      fail(QStringLiteral("The rendered cover text does not meet the warm-white ink threshold; style metadata alone does not prove visible color."));
      return false;
    }
    if (label == QStringLiteral("revised") &&
        (!initial_patch_.isEmpty() && initial_patch_ == preview_->currentAssetBoundAdjustmentPatchJson().toUtf8())) {
      fail(QStringLiteral("The Review revision did not change the saved recipe."));
      return false;
    }
    if (label == QStringLiteral("revised")) {
      const auto initial_layers = evidence_.value(QStringLiteral("initialCoverTextLayers")).toArray();
      if (initial_layers.size() != 3) {
        fail(QStringLiteral("The revision cannot find the initial rendered cover typography record."));
        return false;
      }
      for (int index = 0; index < expected_texts.size(); ++index) {
        const auto before = initial_layers.at(index).toObject().toVariantMap();
        const auto after = verified_texts.at(index).toMap();
        const auto before_bounds = before.value(QStringLiteral("pixelBounds")).toMap();
        const auto after_bounds = after.value(QStringLiteral("pixelBounds")).toMap();
        auto before_style = before.value(QStringLiteral("typography")).toMap();
        auto after_style = after.value(QStringLiteral("typography")).toMap();
        before_style.remove(QStringLiteral("size"));
        after_style.remove(QStringLiteral("size"));
        if (before.value(QStringLiteral("layerId")) != after.value(QStringLiteral("layerId")) ||
            before.value(QStringLiteral("text")) != after.value(QStringLiteral("text")) ||
            before.value(QStringLiteral("color")) != after.value(QStringLiteral("color")) ||
            before_style != after_style) {
          fail(QStringLiteral("The revision changed approved copy, layer identity, color, or non-size typography."));
          return false;
        }
        const auto before_scale = before.value(QStringLiteral("layerScale")).toDouble();
        const auto after_scale = after.value(QStringLiteral("layerScale")).toDouble();
        if (!std::isfinite(before_scale) || !std::isfinite(after_scale) ||
            std::abs(before_scale - after_scale) > 1e-6) {
          fail(QStringLiteral("The revision changed a text layer scale instead of preserving geometry and resizing the headline typography."));
          return false;
        }
        if (index == 1) {
          const auto ratio = after.value(QStringLiteral("effectiveTypographySize")).toDouble() /
              before.value(QStringLiteral("effectiveTypographySize")).toDouble();
          if (std::abs(before_bounds.value(QStringLiteral("x")).toDouble() -
                  after_bounds.value(QStringLiteral("x")).toDouble()) > 1.0 ||
              std::abs(before_bounds.value(QStringLiteral("y")).toDouble() -
                  after_bounds.value(QStringLiteral("y")).toDouble()) > 1.0 ||
              ratio < 0.88 || ratio > 0.92) {
            fail(QStringLiteral("The approved revision must keep the headline position and reduce its size by about 10 percent."));
            return false;
          }
        } else if (before_bounds != after_bounds ||
            before.value(QStringLiteral("typographySize")) != after.value(QStringLiteral("typographySize")) ||
            std::abs(before.value(QStringLiteral("effectiveTypographySize")).toDouble() -
                after.value(QStringLiteral("effectiveTypographySize")).toDouble()) > 1e-6) {
          fail(QStringLiteral("The revision moved or restyled the kicker/footer instead of preserving them."));
          return false;
        }
      }
      evidence_.insert(QStringLiteral("headlineRevisionSizeRatio"),
          verified_texts.at(1).toMap().value(QStringLiteral("typographySize")).toDouble() /
          initial_layers.at(1).toObject().toVariantMap().value(QStringLiteral("typographySize")).toDouble());
    }
    evidence_.insert(label + QStringLiteral("EditorialCoverPassed"), true);
    evidence_.insert(QStringLiteral("editorialCoverPassed"), true);
    evidence_.insert(label + QStringLiteral("CoverTextLayers"), QJsonArray::fromVariantList(verified_texts));
    evidence_.insert(QStringLiteral("verifiedLayerOrder"), QJsonArray::fromVariantList(verified_texts));
    evidence_.insert(QStringLiteral("recipeSha256AtReview"), sha256(preview_->currentAssetBoundAdjustmentPatchJson().toUtf8()));
    evidence_.insert(QStringLiteral("nativeCompositionAtReview"), QJsonObject::fromVariantMap(inspect));
    return writeBytes(QDir(output_root_).filePath(QStringLiteral("reviewed-recipe.json")),
        preview_->currentAssetBoundAdjustmentPatchJson().toUtf8());
  }

  [[nodiscard]] QVariantMap validateRenderedWarmWhiteInk(
      const EditRecipeState& document,
      const QHash<QString, QVariantMap>& visible_layers,
      const QHash<QString, QVariantMap>& recipe_layers,
      const QSize& canvas_size) const {
    const QImage source(options_.source_path);
    if (source.isNull() || canvas_size.isEmpty())
      return {{QStringLiteral("passed"), false},
          {QStringLiteral("error"), QStringLiteral("The original source or cover dimensions are unavailable for rendered ink verification.")}};
    const auto render_export_state = [&](const EditRecipeState& state) {
      const auto output_name = state.output_color_space.trimmed().toLower();
      const auto target = output_name.contains(QStringLiteral("display p3"))
          ? photo_editor::pipeline::DisplayRenderingTarget::DisplayP3Simulation
          : photo_editor::pipeline::DisplayRenderingTarget::SrgbSimulation;
      const auto output_profile = output_name.contains(QStringLiteral("display p3"))
          ? QColorSpace(QColorSpace::DisplayP3)
          : QColorSpace(QColorSpace::SRgb);
      auto export_profile = std::optional<photo_editor::pipeline::AssetIdentifier>{};
      if (!state.export_icc_profile_asset.asset_id.trimmed().isEmpty()) {
        export_profile = photo_editor::pipeline::AssetIdentifier{
            state.export_icc_profile_asset.asset_id.trimmed().toStdString()};
      } else if (!state.export_icc_profile_asset.payload_sha256.trimmed().isEmpty()) {
        export_profile = photo_editor::pipeline::AssetIdentifier{
            "sha256:" + state.export_icc_profile_asset.payload_sha256
                              .trimmed().toStdString()};
      }
      const auto admission = RenderPlanStore::shared().admit(
          options_.asset_id, options_.source_path, state, source.size(),
          photo_editor::pipeline::RenderPurpose::Export, target, export_profile);
      if (!admission.admitted || !admission.plan.has_value()) return QImage{};
      return render_output_image_with_backend(
          source, state, output_profile, QImage::Format_RGBA8888,
          RenderBackendPreference::CpuOnly, nullptr, nullptr, {},
          photo_editor::pipeline::RemovalQuality::Final,
          RenderOutputPolicy::DisplayReferred, &*admission.plan);
    };
    const auto rendered = render_export_state(document);
    const auto baseline = render_export_state(baseline_state_);
    if (rendered.isNull() || baseline.isNull() || rendered.size() != canvas_size ||
        baseline.size() != canvas_size)
      return {{QStringLiteral("passed"), false},
          {QStringLiteral("error"), QStringLiteral("The original and edited CPU renders could not be compared at the cover dimensions.")}};

    QVariantList layer_reports;
    bool all_passed = true;
    for (const auto& text : options_.cover_copy) {
      const auto visible = visible_layers.value(text);
      const auto id = visible.value(QStringLiteral("id")).toString();
      const auto recipe = recipe_layers.value(id);
      QString mask_error;
      const auto glyph_mask = agent_native_text_alpha_mask(
          recipe, canvas_size, &mask_error);
      const auto local = glyph_mask.isNull()
          ? QImage{}
          : rasterize_legacy_photo_text(recipe, {}, nullptr);
      int opaque_changed_glyph_pixels = 0;
      int warm_white_pixels = 0;
      int maximum_red = 0;
      int maximum_green = 0;
      int maximum_blue = 0;
      if (!local.isNull() && !glyph_mask.isNull()) {
        for (int y = 0; y < canvas_size.height(); ++y) {
          const auto* mask_row = reinterpret_cast<const QRgb*>(glyph_mask.constScanLine(y));
          for (int x = 0; x < canvas_size.width(); ++x) {
            if (qAlpha(mask_row[x]) < 230 || rendered.pixel(x, y) == baseline.pixel(x, y)) continue;
            const auto pixel = rendered.pixelColor(x, y);
            ++opaque_changed_glyph_pixels;
            maximum_red = qMax(maximum_red, pixel.red());
            maximum_green = qMax(maximum_green, pixel.green());
            maximum_blue = qMax(maximum_blue, pixel.blue());
            if (pixel.red() >= 210 && pixel.green() >= 200 && pixel.blue() >= 170 &&
                pixel.red() >= pixel.blue() - 20) ++warm_white_pixels;
          }
        }
      }
      const bool passed = mask_error.isEmpty() && opaque_changed_glyph_pixels >= 50 &&
          warm_white_pixels * 100 >= opaque_changed_glyph_pixels * 85;
      all_passed = all_passed && passed;
      layer_reports.append(QVariantMap{{QStringLiteral("text"), text},
          {QStringLiteral("layerId"), id},
          {QStringLiteral("opaqueChangedGlyphPixels"), opaque_changed_glyph_pixels},
          {QStringLiteral("warmWhitePixels"), warm_white_pixels},
          {QStringLiteral("maximumRenderedRgb"), QVariantList{maximum_red, maximum_green, maximum_blue}},
          {QStringLiteral("passed"), passed}, {QStringLiteral("maskError"), mask_error}});
    }
    return {{QStringLiteral("passed"), all_passed},
        {QStringLiteral("minimumRgb"), QVariantList{210, 200, 170}},
        {QStringLiteral("minimumOpaqueChangedGlyphPixels"), 50},
        {QStringLiteral("minimumWarmWhiteCoveragePercent"), 85},
        {QStringLiteral("layers"), layer_reports}};
  }

  [[nodiscard]] QImage transformedAlpha(const QVariantMap& layer, const QImage& local,
      const QSize& canvas_size, QString* error) const {
    if (local.isNull() || canvas_size.isEmpty()) {
      if (error) *error = QStringLiteral("The composition alpha raster is empty.");
      return {};
    }
    QImage full(canvas_size, QImage::Format_ARGB32_Premultiplied);
    if (full.isNull()) {
      if (error) *error = QStringLiteral("Could not allocate the composition overlap raster.");
      return {};
    }
    full.fill(Qt::transparent);
    const auto scale = qBound(0.05, layer.value(QStringLiteral("scale"), 1.0).toDouble(), 32.0);
    const auto rotation = layer.value(QStringLiteral("rotation"), 0.0).toDouble();
    const auto x = layer.value(QStringLiteral("x")).toDouble();
    const auto y = layer.value(QStringLiteral("y")).toDouble();
    const auto pixel_width = layer.value(QStringLiteral("pixelWidth"), local.width()).toDouble();
    const auto pixel_height = layer.value(QStringLiteral("pixelHeight"), local.height()).toDouble();
    QPainter painter(&full);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.translate(x + pixel_width * scale * 0.5, y + pixel_height * scale * 0.5);
    painter.rotate(rotation);
    painter.scale(scale, scale);
    painter.drawImage(QPointF(-local.width() * 0.5, -local.height() * 0.5), local);
    painter.end();
    return full;
  }

  [[nodiscard]] bool validateComposition(const QString& label) {
    const auto parsed = parse_adjustment_patch_json(preview_->currentAssetBoundAdjustmentPatchJson());
    if (!parsed.ok() || !parsed.state.has_value()) {
      fail(QStringLiteral("The reviewed native asset recipe could not be parsed."));
      return false;
    }
    const auto expected_copy = label == QStringLiteral("revised")
        ? options_.revision_exact_copy : options_.exact_copy;
    const auto inspect = preview_->photoCompositionCommand({
        {QStringLiteral("assetId"), options_.asset_id},
        {QStringLiteral("operation"), QStringLiteral("inspect")}});
    if (!inspect.value(QStringLiteral("ok")).toBool() ||
        inspect.value(QStringLiteral("order")).toString() != QStringLiteral("top-first")) {
      fail(QStringLiteral("The current native composition inspection did not return a certified top-first stack."));
      return false;
    }
    const auto layers = inspect.value(QStringLiteral("layers")).toList();
    const auto masks = inspect.value(QStringLiteral("masks")).toList();
    QHash<QString, QVariantMap> recipe_layers;
    for (const auto& value : parsed.state->layers) {
      const auto layer = value.toMap();
      recipe_layers.insert(layer.value(QStringLiteral("id")).toString(), layer);
    }
    if (isEditorialCover())
      return validateEditorialCoverComposition(label, inspect, layers, recipe_layers, *parsed.state);

    const auto initial_report = label == QStringLiteral("initial")
        ? edit_->lastRunReport() : initial_run_report_;
    const auto add_text = successfulCompositionReceipt(
        initial_report, QStringLiteral("add_text"), expected_copy);
    const auto select_subject = successfulCompositionReceipt(initial_report, QStringLiteral("select_subject"));
    const auto copy_selection = successfulCompositionReceipt(initial_report, QStringLiteral("copy_selection"));
    const auto add_text_content = add_text.value(QStringLiteral("structuredContent")).toMap();
    const auto copy_content = copy_selection.value(QStringLiteral("structuredContent")).toMap();
    const auto copy_args = copy_selection.value(QStringLiteral("arguments")).toMap();
    const auto mask_id = copy_args.value(QStringLiteral("values")).toMap().value(QStringLiteral("maskId")).toString();
    const auto independent = copy_args.value(QStringLiteral("values")).toMap().value(QStringLiteral("independent")).toBool();
    if (label == QStringLiteral("initial")) {
      if (add_text.isEmpty() || select_subject.isEmpty() || copy_selection.isEmpty() ||
          add_text_content.value(QStringLiteral("changed")).toBool() != true ||
          copy_content.value(QStringLiteral("changed")).toBool() != true || !independent || mask_id.isEmpty()) {
        fail(QStringLiteral("The initial run report lacks successful native exact-copy, subject-selection, and independent-copy receipts."));
        return false;
      }
      initial_run_report_ = initial_report;
    }
    const auto text_id = add_text_content.value(QStringLiteral("createdLayerId")).toString();
    const auto cutout_id = copy_content.value(QStringLiteral("createdLayerId")).toString();
    if (text_id.isEmpty() || cutout_id.isEmpty() || baseline_layer_ids_.contains(text_id) ||
        baseline_layer_ids_.contains(cutout_id)) {
      fail(QStringLiteral("The task receipts do not identify newly created stable headline and cutout layer IDs."));
      return false;
    }
    bool mask_present = false;
    for (const auto& mask_value : masks) {
      const auto mask = mask_value.toMap();
      if (mask.value(QStringLiteral("id")).toString() == mask_id) mask_present = true;
    }
    if (!mask_present || baseline_mask_ids_.contains(mask_id)) {
      fail(QStringLiteral("The cutout receipt does not identify a new selection mask retained in the current native document."));
      return false;
    }

    QVariantMap roles;
    for (const auto& layer_value : layers) {
      const auto layer = layer_value.toMap();
      const auto id = layer.value(QStringLiteral("id")).toString();
      if (id == text_id && layer.value(QStringLiteral("kind")).toString() == QStringLiteral("text") &&
          layer.value(QStringLiteral("enabled"), true).toBool() &&
          layer.value(QStringLiteral("text")).toString() == expected_copy)
        roles.insert(QStringLiteral("text"), QVariantMap{{QStringLiteral("index"), layer.value(QStringLiteral("position"))},
            {QStringLiteral("id"), id}, {QStringLiteral("name"), layer.value(QStringLiteral("name"))},
            {QStringLiteral("enabled"), true}});
      if (id == cutout_id && layer.value(QStringLiteral("kind")).toString() == QStringLiteral("pixel")) {
        const auto full_layer = recipe_layers.value(id);
        const auto paint_tiles = full_layer.value(QStringLiteral("paintTiles")).toMap();
        const auto tiles = paint_tiles.value(QStringLiteral("tiles")).toMap();
        if (layer.value(QStringLiteral("enabled"), true).toBool() &&
            !full_layer.value(QStringLiteral("selectionScoped")).toBool() &&
            !full_layer.value(QStringLiteral("selectionMaskSetId")).isValid() && !tiles.isEmpty())
          roles.insert(QStringLiteral("cutout"), QVariantMap{{QStringLiteral("index"), layer.value(QStringLiteral("position"))},
              {QStringLiteral("id"), id}, {QStringLiteral("name"), layer.value(QStringLiteral("name"))},
              {QStringLiteral("maskSetId"), mask_id}, {QStringLiteral("paintTileCount"), tiles.size()},
              {QStringLiteral("enabled"), true}});
      }
    }
    if (!roles.contains(QStringLiteral("text")) || !roles.contains(QStringLiteral("cutout"))) {
      fail(QStringLiteral("The native composition lacks the receipt-bound exact headline or independently materialized pixel cutout."));
      return false;
    }
    QVariantList order;
    int previous_index = -1;
    for (const auto& role : options_.top_to_bottom_roles) {
      const auto layer = roles.value(role).toMap();
      const auto index = layer.value(QStringLiteral("index"), -1).toInt();
      if (layer.isEmpty() || index < 0 || (previous_index >= 0 && index <= previous_index)) {
        fail(QStringLiteral("The native layer stack does not match the approved top-to-bottom ordering."));
        return false;
      }
      previous_index = index;
      order.append(QVariantMap{{QStringLiteral("role"), role}, {QStringLiteral("index"), index},
          {QStringLiteral("layerId"), layer.value(QStringLiteral("id"))},
          {QStringLiteral("name"), layer.value(QStringLiteral("name"))},
          {QStringLiteral("maskSetId"), layer.value(QStringLiteral("maskSetId"))}});
    }
    evidence_.insert(QStringLiteral("verifiedLayerOrder"), QJsonArray::fromVariantList(order));
    const auto text_layer = recipe_layers.value(text_id);
    const auto cutout_layer = recipe_layers.value(cutout_id);
    const auto text_raster = rasterize_legacy_photo_text(text_layer, {}, nullptr);
    const auto cutout_raster = rasterize_photo_paint_layer(cutout_layer, {}, nullptr);
    if (text_raster.isNull() || cutout_raster.isNull()) {
      fail(QStringLiteral("The actual headline or independent cutout pixel data could not be rasterized."));
      return false;
    }
    const QSize canvas_size(inspect.value(QStringLiteral("width")).toInt(), inspect.value(QStringLiteral("height")).toInt());
    QString alpha_error;
    const auto text_alpha = transformedAlpha(text_layer, text_raster, canvas_size, &alpha_error);
    const auto cutout_alpha = transformedAlpha(cutout_layer, cutout_raster, canvas_size, &alpha_error);
    if (text_alpha.isNull() || cutout_alpha.isNull()) {
      fail(QStringLiteral("The headline/cutout geometry could not be rendered for overlap verification: %1").arg(alpha_error));
      return false;
    }
    const auto crop = crop_rect_for_image(canvas_size, *parsed.state).intersected(QRect(QPoint(), canvas_size));
    if (crop.isEmpty()) {
      fail(QStringLiteral("The reviewed thumbnail crop is empty."));
      return false;
    }
    qint64 overlap_pixels = 0;
    QRect overlap_bounds;
    for (int y = crop.top(); y <= crop.bottom(); ++y) {
      const auto* text_row = reinterpret_cast<const QRgb*>(text_alpha.constScanLine(y));
      const auto* cutout_row = reinterpret_cast<const QRgb*>(cutout_alpha.constScanLine(y));
      for (int x = crop.left(); x <= crop.right(); ++x) {
        if (qAlpha(text_row[x]) < 32 || qAlpha(cutout_row[x]) < 128) continue;
        ++overlap_pixels;
        const QRect pixel(x, y, 1, 1);
        overlap_bounds = overlap_bounds.isNull() ? pixel : overlap_bounds.united(pixel);
      }
    }
    if (overlap_pixels < 100) {
      fail(QStringLiteral("The independent subject pixels do not overlap enough of the headline in the visible crop."));
      return false;
    }
    if (overlap_bounds.center().x() <= canvas_size.width() / 2) {
      fail(QStringLiteral("The headline/cutout overlap does not land over the intended performer on the right side of the source."));
      return false;
    }
    evidence_.insert(QStringLiteral("headlineCutoutOverlapPixels"), overlap_pixels);
    evidence_.insert(QStringLiteral("headlineCutoutOverlapBounds"), QJsonObject{
        {QStringLiteral("x"), overlap_bounds.x()}, {QStringLiteral("y"), overlap_bounds.y()},
        {QStringLiteral("width"), overlap_bounds.width()}, {QStringLiteral("height"), overlap_bounds.height()}});
    evidence_.insert(QStringLiteral("headlineCutoutOverlapInsideCrop"), true);
    QStringList changed_color_fields;
    if (!hasActualColorChange(baseline_state_, *parsed.state, &changed_color_fields)) {
      fail(QStringLiteral("The reviewed composition has no actual color adjustment change from its original recipe."));
      return false;
    }
    evidence_.insert(QStringLiteral("changedColorFields"), QJsonArray::fromStringList(changed_color_fields));
    evidence_.insert(QStringLiteral("recipeSha256AtReview"), sha256(preview_->currentAssetBoundAdjustmentPatchJson().toUtf8()));
    evidence_.insert(QStringLiteral("nativeCompositionAtReview"), QJsonObject::fromVariantMap(inspect));
    evidence_.insert(QStringLiteral("maskSetSnapshotAtReview"), QJsonArray::fromVariantList(masks));
    return writeBytes(QDir(output_root_).filePath(QStringLiteral("reviewed-recipe.json")),
        preview_->currentAssetBoundAdjustmentPatchJson().toUtf8());
  }

  [[nodiscard]] bool requireHealthySession() {
    const auto stage = session_->stage();
    if (stage == QStringLiteral("error") || stage == QStringLiteral("interrupted") ||
        stage == QStringLiteral("clarifying")) {
      const auto reason = session_->status().trimmed();
      fail(QStringLiteral("The actual model/session workflow stopped at %1: %2")
          .arg(stage, reason.isEmpty() ? QStringLiteral("no additional owner detail") : reason));
      return false;
    }
    return true;
  }

  [[nodiscard]] bool dismissFirstRunGuideIfVisible() {
    auto* guide = qmlObject(QStringLiteral("firstRunGuideDialog"));
    if (guide && guide->property("visible").toBool()) {
      if (first_run_skip_requested_) return false;
      if (!click(QStringLiteral("firstRunSkipButton"))) {
        fail(QStringLiteral("The visible first-run guide could not be dismissed through its Skip control."));
        return false;
      }
      first_run_skip_requested_ = true;
      static_cast<void>(writeCheckpoint(QStringLiteral("first_run_skip_requested")));
      return false;
    }
    if (first_run_skip_requested_) {
      first_run_skip_requested_ = false;
      static_cast<void>(writeCheckpoint(QStringLiteral("first_run_guide_skipped")));
    }
    return !finished_;
  }

  void openVisibleTerminal() {
    if (pending_capture_.active && pending_capture_.purpose == PresentedCapturePurpose::Request) {
      const auto capture_result = captureAfterPresentation(QStringLiteral("request"), PresentedCapturePurpose::Request);
      if (capture_result == PresentedCaptureResult::Failed) {
        fail(QStringLiteral("The actual terminal request screen could not be captured: %1")
                 .arg(last_capture_failure_detail_));
        return;
      }
      if (capture_result == PresentedCaptureResult::Pending) return;
    }
    if (!dismissFirstRunGuideIfVisible()) return;
        if (auto* terminal = qmlObject(QStringLiteral("agentSessionTerminal"));
        terminal && terminal->property("visible").toBool()) {
      auto* dialog = qmlObject(QStringLiteral("agentRunDialog"));
      if (!dialog) return;
      if (!dialog->property("visible").toBool()) return;
      terminal_opened_ = true;
      dialog->setProperty("terminalPrompt", options_.initial_prompt);
      const auto capture_result = captureAfterPresentation(
          QStringLiteral("request"), PresentedCapturePurpose::Request);
      if (capture_result == PresentedCaptureResult::Pending) return;
      if (capture_result == PresentedCaptureResult::Failed) {
        fail(QStringLiteral("The actual terminal request screen could not be captured: %1")
                 .arg(last_capture_failure_detail_));
        return;
      }
      if (!click(QStringLiteral("agentSessionSubmitButton"))) {
        fail(QStringLiteral("The actual terminal request screen could not submit through its visible Continue button."));
        return;
      }
      phase_ = Phase::PlanningCancel;
      static_cast<void>(writeCheckpoint(QStringLiteral("initial_request_submitted_for_cancel_proof")));
      return;
    }
    if (terminal_opened_) return;
    if (terminal_open_requested_) return;
    const auto is_edit_workspace = window_->property("currentWorkspace").toInt() ==
        window_->property("editWorkspaceIndex").toInt();
    QObject* dock = is_edit_workspace
        ? qmlObject(QStringLiteral("editAgentButton"))
        : qmlObject(QStringLiteral("libraryAgentButton"));
    if (!dock || !QMetaObject::invokeMethod(dock, "activated", Qt::DirectConnection)) {
      fail(QStringLiteral("The visible Looksy Agent dock entry could not open the real session dialog."));
      return;
    }
    terminal_open_requested_ = true;
    static_cast<void>(writeCheckpoint(QStringLiteral("agent_dialog_open_requested")));
  }

  void advance() {
    if (!started_ || finished_ || advancing_) return;
    advancing_ = true;
    switch (phase_) {
      case Phase::WaitingForModelProbe: {
        const auto model = installedLocalModel(options_.installed_model_id);
        if (model.isEmpty()) {
          fail(QStringLiteral("The selected local model disappeared from the installed-model catalog."));
          break;
        }
        if (edit_->modelProbeRunning()) break;
        const auto completed_probe_at = model.value(QStringLiteral("lastProbeAtUtc")).toString();
        if (completed_probe_at.isEmpty() || completed_probe_at == prior_model_probe_at_utc_) break;
        QVariantMap probe_evidence{
            {QStringLiteral("contract"), QStringLiteral("agent-actual-local-model-probe-v1")},
            {QStringLiteral("modelId"), options_.installed_model_id},
            {QStringLiteral("requestedAtUtc"), model_probe_requested_at_utc_},
            {QStringLiteral("completedAtUtc"), completed_probe_at},
            {QStringLiteral("backend"), model.value(QStringLiteral("backend"))},
            {QStringLiteral("runtime"), model.value(QStringLiteral("runtime"))},
            {QStringLiteral("modelName"), model.value(QStringLiteral("model"))},
            {QStringLiteral("available"), model.value(QStringLiteral("available"))},
            {QStringLiteral("status"), model.value(QStringLiteral("status"))},
            {QStringLiteral("statusReason"), model.value(QStringLiteral("statusReason"))},
            {QStringLiteral("modelFamily"), model.value(QStringLiteral("modelFamily"))},
            {QStringLiteral("parameterSize"), model.value(QStringLiteral("parameterSize"))},
            {QStringLiteral("quantizationLevel"), model.value(QStringLiteral("quantizationLevel"))},
        };
        evidence_.insert(QStringLiteral("modelProbe"), QJsonObject::fromVariantMap(probe_evidence));
        static_cast<void>(writeCheckpoint(QStringLiteral("local_model_probe_completed")));
        if (!model.value(QStringLiteral("available")).toBool()) {
          const auto reason = model.value(QStringLiteral("statusReason")).toString().trimmed();
          fail(reason.isEmpty()
              ? QStringLiteral("The completed local Ollama probe reports that the requested model is unavailable.")
              : QStringLiteral("The completed local Ollama probe failed: %1").arg(reason));
          break;
        }
        phase_ = Phase::WaitingForAsset;
        break;
      }
      case Phase::WaitingForAsset: {
        if (catalog_->loading()) break;
        const auto asset = catalog_->assetById(options_.asset_id);
        if (asset.isEmpty() || QFileInfo(asset.value(QStringLiteral("filePath")).toString()).canonicalFilePath() != options_.source_path) {
          fail(QStringLiteral("The supplied imported asset ID does not resolve to the supplied source photo."));
          break;
        }
        if (catalog_->selectedAssetId() != options_.asset_id) {
          if (!catalog_->selectAssetById(options_.asset_id, true)) {
            fail(QStringLiteral("The explicit imported photo could not be selected in the real catalog."));
            break;
          }
          break;
        }
        if (!asset_edit_activation_requested_) {
          auto* library_workspace = qmlObject(QStringLiteral("libraryStudioWorkspace"));
          if (!library_workspace || !library_workspace->property("visible").toBool() ||
              !QMetaObject::invokeMethod(library_workspace, "assetActivated", Qt::DirectConnection,
                  Q_ARG(QString, QStringLiteral("edit")))) {
            fail(QStringLiteral("The selected photo could not enter Edit through the visible Library workspace route."));
            break;
          }
          asset_edit_activation_requested_ = true;
          evidence_.insert(QStringLiteral("assetActivationRoute"),
              QStringLiteral("libraryStudioWorkspace.assetActivated(edit)"));
          const auto edit_index = window_->property("editWorkspaceIndex").toInt();
          const auto current_workspace = window_->property("currentWorkspace").toInt();
          const auto transition_active = window_->property("workspaceTransitionActive").toBool();
          const auto pending_workspace = window_->property("pendingWorkspaceIndex").toInt();
          evidence_.insert(QStringLiteral("editRouteTransitionRequested"),
              (transition_active && pending_workspace == edit_index) || current_workspace == edit_index);
          evidence_.insert(QStringLiteral("editRoutePendingWorkspace"), pending_workspace);
          if (!transition_active && current_workspace != edit_index) {
            fail(QStringLiteral("The real Library Edit activation did not request the shell transition to Edit."));
            break;
          }
          if (transition_active && pending_workspace != edit_index) {
            fail(QStringLiteral("The real Library Edit activation requested a different workspace transition."));
            break;
          }
          static_cast<void>(writeCheckpoint(QStringLiteral("library_asset_activated_for_edit")));
        }
        const auto edit_index = window_->property("editWorkspaceIndex").toInt();
        const auto current_workspace = window_->property("currentWorkspace").toInt();
        const auto transition_active = window_->property("workspaceTransitionActive").toBool();
        const auto pending_workspace = window_->property("pendingWorkspaceIndex").toInt();
        if (transition_active && pending_workspace != edit_index) {
          fail(QStringLiteral("The requested Edit transition was superseded before the proof workspace loaded."));
          break;
        }
        if (window_->property("appActivityErrorVisible").toBool()) {
          const auto detail = window_->property("appActivityStatusDetail").toString().trimmed();
          fail(detail.isEmpty()
              ? QStringLiteral("The actual Edit workspace reported a load failure.")
              : QStringLiteral("The actual Edit workspace failed to load: %1").arg(detail));
          break;
        }
        auto* edit_workspace = qmlObject(QStringLiteral("editStudioWorkspace"));
        if (preview_->assetId() != options_.asset_id || !preview_->hasAsset() ||
            current_workspace != edit_index || !edit_workspace ||
            transition_active ||
            !edit_workspace->property("workspaceActive").toBool() ||
            !edit_workspace->property("pageContentReady").toBool()) break;
        if (!dismissFirstRunGuideIfVisible()) break;
        if (!preview_->recipeSaveError().isEmpty()) {
          fail(QStringLiteral("The initial Preview recipe save failed: %1").arg(preview_->recipeSaveError()));
          break;
        }
        if (preview_->recipeSavePending() ||
            !catalog_->agentTaskRecipePersistenceSettled(options_.asset_id)) {
          if (!initial_recipe_persistence_wait_recorded_) {
            static_cast<void>(writeCheckpoint(QStringLiteral("waiting_for_initial_recipe_persistence")));
            initial_recipe_persistence_wait_recorded_ = true;
          }
          break;
        }
        source_sha256_ = evidence_.value(QStringLiteral("sourceSha256")).toString();
        if (sha256(readFile(options_.source_path)) != source_sha256_) {
          fail(QStringLiteral("The private source image changed after fixture admission."));
          break;
        }
        beginBaselineExport();
        break;
      }
      case Phase::BaselineExport: {
        if (!exportSettled()) break;
        QString error;
        if (!readExport(&baseline_image_, &error)) { fail(error); break; }
        evidence_.insert(QStringLiteral("baselineExportPath"), export_path_);
        evidence_.insert(QStringLiteral("baselineExportSha256"), sha256(readFile(export_path_)));
        const auto baseline_capture = captureAfterPresentation(
            QStringLiteral("workspace-baseline"), PresentedCapturePurpose::Workspace);
        if (baseline_capture == PresentedCaptureResult::Pending) break;
        if (baseline_capture == PresentedCaptureResult::Failed) {
          fail(QStringLiteral("The actual app workspace baseline screenshot could not be captured: %1")
                   .arg(last_capture_failure_detail_));
          break;
        }
        phase_ = Phase::OpenTerminal;
        static_cast<void>(writeCheckpoint(QStringLiteral("baseline_exported")));
        break;
      }
      case Phase::OpenTerminal:
        openVisibleTerminal();
        break;
      case Phase::PlanningCancel:
        if (session_->stage() == QStringLiteral("planning")) {
          if (!click(QStringLiteral("agentSessionCancelPlanningButton"))) {
            fail(QStringLiteral("The visible Stop planning control was unavailable during real model planning."));
            break;
          }
          planning_cancel_requested_at_ms_ = QDateTime::currentMSecsSinceEpoch();
          evidence_.insert(QStringLiteral("planningCancellationRequestedFromVisibleStopControl"), true);
          phase_ = Phase::PlanningCancelSettled;
          static_cast<void>(writeCheckpoint(QStringLiteral("visible_planning_cancel_requested")));
        } else if (session_->stage() == QStringLiteral("plan-ready")) {
          fail(QStringLiteral("The model plan completed before the visible planning Stop control could be exercised."));
        } else if (session_->stage() == QStringLiteral("error") ||
                   session_->stage() == QStringLiteral("clarifying") ||
                   session_->stage() == QStringLiteral("interrupted")) {
          fail(QStringLiteral("The session did not enter active planning for the cancellation proof: %1").arg(session_->stage()));
        }
        break;
      case Phase::PlanningCancelSettled: {
        const auto elapsed_ms = QDateTime::currentMSecsSinceEpoch() - planning_cancel_requested_at_ms_;
        if (session_->stage() != QStringLiteral("interrupted")) {
          if (elapsed_ms >= 1500) fail(QStringLiteral("Planning Stop did not settle to interrupted state without a late plan."));
          break;
        }
        if (edit_->draftActive() || preview_->currentAssetBoundAdjustmentPatchJson().toUtf8() != baseline_preview_ ||
            catalog_->agentTaskRecipeResourceAdapter(options_.asset_id).read() != baseline_catalog_) {
          fail(QStringLiteral("Canceling planning changed the native draft, Preview recipe, or persisted Catalog recipe."));
          break;
        }
        if (elapsed_ms < 1500) break;
        evidence_.insert(QStringLiteral("planningCancellationSettledInterrupted"), true);
        evidence_.insert(QStringLiteral("planningCancellationHadNoLatePlanOrMutation"), true);
        evidence_.insert(QStringLiteral("stopBehaviorProven"), true);
        evidence_.insert(QStringLiteral("stopBehaviorScope"), QStringLiteral("visible planning cancellation before edit execution"));
        const auto cancel_capture = captureAfterPresentation(
            QStringLiteral("planning-cancelled"), PresentedCapturePurpose::PlanningCancelled);
        if (cancel_capture == PresentedCaptureResult::Pending) break;
        if (cancel_capture == PresentedCaptureResult::Failed) {
          fail(QStringLiteral("The visible planning-cancellation screen could not be captured: %1")
                   .arg(last_capture_failure_detail_));
          break;
        }
        if (!click(QStringLiteral("agentSessionRetryButton"))) {
          fail(QStringLiteral("The visible retry control could not restart the same request after cancellation."));
          break;
        }
        evidence_.insert(QStringLiteral("retryUsedVisibleControl"), true);
        phase_ = Phase::InitialPlan;
        static_cast<void>(writeCheckpoint(QStringLiteral("cancelled_request_retried_from_ui")));
        break;
      }
      case Phase::InitialPlan:
        if (!requireHealthySession()) break;
        if (session_->stage() != QStringLiteral("plan-ready")) break;
        if (validatePlan(QStringLiteral("initial"))) {
          if (!click(QStringLiteral("agentSessionStartButton"))) {
            fail(QStringLiteral("The initial approved plan could not be started through the visible Start button."));
            break;
          }
          phase_ = Phase::InitialExecution;
          static_cast<void>(writeCheckpoint(QStringLiteral("initial_plan_started_from_ui")));
        } else if (!finished_ && !hasPendingPresentedCapture()) {
          fail(QStringLiteral("The initial approved plan could not be captured or started through the visible Start button."));
        }
        break;
      case Phase::InitialExecution:
        if (!requireHealthySession()) break;
        if (session_->stage() != QStringLiteral("review")) break;
        if (!validateReview(QStringLiteral("initial"), true)) break;
        phase_ = Phase::InitialPersistence;
        static_cast<void>(writeCheckpoint(QStringLiteral("initial_review_waiting_for_recipe_settlement")));
        break;
      case Phase::InitialPersistence: {
        if (preview_->recipeSavePending() || !catalog_->agentTaskRecipePersistenceSettled(options_.asset_id)) break;
        if (!preview_->recipeSaveError().isEmpty()) {
          fail(QStringLiteral("The initial reviewed task save failed: %1").arg(preview_->recipeSaveError()));
          break;
        }
        initial_patch_ = preview_->currentAssetBoundAdjustmentPatchJson().toUtf8();
        if (initial_patch_.isEmpty() || catalog_->assetById(options_.asset_id)
                .value(QStringLiteral("adjustmentPatchJson")).toString().toUtf8() != initial_patch_) {
          fail(QStringLiteral("The initial Review recipe did not settle to the same Catalog bytes."));
          break;
        }
        if (!writeBytes(QDir(output_root_).filePath(QStringLiteral("initial-reviewed-recipe.json")), initial_patch_)) {
          fail(QStringLiteral("Could not retain the initial reviewed recipe bytes."));
          break;
        }
        evidence_.insert(QStringLiteral("initialReviewedRecipeSha256"), sha256(initial_patch_));
        if (auto* revision = qmlObject(QStringLiteral("agentSessionRevisionPrompt")))
          revision->setProperty("text", options_.revision_prompt);
        if (!click(QStringLiteral("agentSessionReviseButton"))) {
          fail(QStringLiteral("The actual Review screen could not submit the approved revision through its visible button."));
          break;
        }
        phase_ = Phase::RevisionPlan;
        static_cast<void>(writeCheckpoint(QStringLiteral("revision_submitted_from_ui")));
        break;
      }
      case Phase::RevisionPlan:
        if (!requireHealthySession()) break;
        if (session_->stage() != QStringLiteral("plan-ready")) break;
        if (validatePlan(QStringLiteral("revision"))) {
          if (!click(QStringLiteral("agentSessionStartButton"))) {
            fail(QStringLiteral("The approved revision could not be started through the visible Start button."));
            break;
          }
          phase_ = Phase::RevisionExecution;
          static_cast<void>(writeCheckpoint(QStringLiteral("revision_plan_started_from_ui")));
        } else if (!finished_ && !hasPendingPresentedCapture()) {
          fail(QStringLiteral("The approved revision could not be captured or started through the visible Start button."));
        }
        break;
      case Phase::RevisionExecution:
        if (!requireHealthySession()) break;
        if (session_->stage() != QStringLiteral("review")) break;
        if (!validateReview(QStringLiteral("revised"), true)) break;
        if (edit_->taskTransaction().value(QStringLiteral("status")).toString() != QStringLiteral("review")) break;
        phase_ = Phase::RevisionPersistence;
        static_cast<void>(writeCheckpoint(QStringLiteral("revision_review_waiting_for_recipe_settlement")));
        break;
      case Phase::RevisionPersistence:
        if (preview_->recipeSavePending() || !catalog_->agentTaskRecipePersistenceSettled(options_.asset_id)) break;
        if (!preview_->recipeSaveError().isEmpty()) {
          fail(QStringLiteral("The native Preview save failed: %1").arg(preview_->recipeSaveError()));
          break;
        }
        if (preview_->currentAssetBoundAdjustmentPatchJson().toUtf8() !=
            catalog_->assetById(options_.asset_id).value(QStringLiteral("adjustmentPatchJson")).toString().toUtf8()) {
          fail(QStringLiteral("The settled revision Catalog recipe differs from the current Preview recipe."));
          break;
        }
        static_cast<void>(writeCheckpoint(QStringLiteral("revised_reviewed_and_save_settled")));
        phase_ = Phase::EditedExport;
        if (!beginExport(QStringLiteral("edited-before-reopen"))) fail(QStringLiteral("The revised native export could not start."));
        break;
      case Phase::EditedExport: {
        if (preview_->recipeSavePending() || !catalog_->agentTaskRecipePersistenceSettled(options_.asset_id)) break;
        if (!exportSettled()) break;
        QString error;
        if (!readExport(&edited_image_, &error)) { fail(error); break; }
        const auto aspect_error = std::abs(
            edited_image_.width() * options_.target_aspect_height -
            edited_image_.height() * options_.target_aspect_width);
        const bool wrong_output_geometry = isEditorialCover()
            ? edited_image_.size() != baseline_image_.size()
            : (edited_image_.width() < options_.minimum_output_width ||
               edited_image_.height() < options_.minimum_output_height ||
               aspect_error > std::max(options_.target_aspect_width, options_.target_aspect_height));
        if (edited_image_ == baseline_image_ || wrong_output_geometry) {
          fail(QStringLiteral("The reviewed native export did not produce changed pixels at the requested resolution and aspect ratio."));
          break;
        }
        if (isEditorialCover()) {
          if (cover_text_layers_.size() != 3) {
            fail(QStringLiteral("The final cover export is missing reviewed native text layers."));
            break;
          }
          QVariantList native_text_layers;
          const auto export_recipe_state = parse_edit_recipe_json(
              preview_->currentAssetBoundAdjustmentPatchJson());
          for (const auto& value : export_recipe_state.layers) {
            const auto layer = value.toMap();
            if (layer.value(QStringLiteral("kind")).toString() == QStringLiteral("text"))
              native_text_layers.append(layer);
          }
          if (native_text_layers.size() != 3) {
            fail(QStringLiteral("The current recipe does not contain the three native text rasters used by the reviewed cover."));
            break;
          }
          QString native_mask_error;
          const auto native_text_coverage = agent_native_text_coverage_mask(
              native_text_layers, edited_image_.size(), &native_mask_error);
          bool background_preserved = !native_text_coverage.isNull();
          auto changed_photo_pixels_outside_native_text = std::int64_t{0};
          auto maximum_photo_channel_delta_outside_native_text = 0;
          for (int y = 0; y < edited_image_.height() && background_preserved; ++y) {
            for (int x = 0; x < edited_image_.width(); ++x) {
              if (qAlpha(native_text_coverage.pixel(x, y)) != 0) continue;
              const auto before = baseline_image_.pixelColor(x, y);
              const auto after = edited_image_.pixelColor(x, y);
              if (before == after) continue;
              ++changed_photo_pixels_outside_native_text;
              maximum_photo_channel_delta_outside_native_text = qMax(
                  maximum_photo_channel_delta_outside_native_text,
                  qMax(qAbs(before.red() - after.red()),
                      qMax(qAbs(before.green() - after.green()),
                          qAbs(before.blue() - after.blue()))));
            }
          }
          background_preserved = background_preserved &&
              changed_photo_pixels_outside_native_text == 0;
          if (!background_preserved) {
            fail(native_mask_error.isEmpty()
                ? QStringLiteral("The editorial cover changed photograph pixels outside the actual native text raster (changedPixels=%1, maxChannelDelta=%2).")
                      .arg(changed_photo_pixels_outside_native_text)
                      .arg(maximum_photo_channel_delta_outside_native_text)
                : QStringLiteral("The editorial cover's native text raster could not be verified: %1")
                      .arg(native_mask_error));
            break;
          }
          evidence_.insert(QStringLiteral("backgroundPixelsPreservedOutsideNativeTextRaster"), true);
          evidence_.insert(QStringLiteral("changedPhotoPixelsOutsideNativeText"),
              changed_photo_pixels_outside_native_text);
          evidence_.insert(QStringLiteral("maximumPhotoChannelDeltaOutsideNativeText"),
              maximum_photo_channel_delta_outside_native_text);
          evidence_.insert(QStringLiteral("coverOutputDimensionsMatchSource"), true);
        }
        edited_patch_ = preview_->currentAssetBoundAdjustmentPatchJson().toUtf8();
        const auto asset = catalog_->assetById(options_.asset_id);
        if (edited_patch_.isEmpty() || asset.value(QStringLiteral("adjustmentPatchJson")).toString().toUtf8() != edited_patch_) {
          fail(QStringLiteral("The settled Catalog recipe does not exactly match the reviewed Preview recipe."));
          break;
        }
        const auto edited_state = parse_adjustment_patch_json(QString::fromUtf8(edited_patch_));
        if (!edited_state.ok() || !edited_state.state.has_value()) {
          fail(QStringLiteral("The final exported recipe could not be parsed for crop verification."));
          break;
        }
        const QSize source_size(baseline_image_.width(), baseline_image_.height());
        const auto original_crop = crop_rect_for_image(source_size, baseline_state_);
        const auto edited_crop = crop_rect_for_image(source_size, *edited_state.state);
        if ((isEditorialCover() && original_crop != edited_crop) ||
            (!isEditorialCover() && original_crop == edited_crop) || edited_crop.isEmpty()) {
          fail(isEditorialCover()
              ? QStringLiteral("The editorial cover changed the original photo framing.")
              : QStringLiteral("The native thumbnail framing did not change the original photo crop."));
          break;
        }
        evidence_.insert(QStringLiteral("baselineCrop"), QJsonObject{
            {QStringLiteral("x"), original_crop.x()}, {QStringLiteral("y"), original_crop.y()},
            {QStringLiteral("width"), original_crop.width()}, {QStringLiteral("height"), original_crop.height()}});
        evidence_.insert(QStringLiteral("editedCrop"), QJsonObject{
            {QStringLiteral("x"), edited_crop.x()}, {QStringLiteral("y"), edited_crop.y()},
            {QStringLiteral("width"), edited_crop.width()}, {QStringLiteral("height"), edited_crop.height()}});
        evidence_.insert(QStringLiteral("editedExportDimensions"), QJsonObject{
            {QStringLiteral("width"), edited_image_.width()}, {QStringLiteral("height"), edited_image_.height()}});
        evidence_.insert(QStringLiteral("editedExportAspectErrorNumerator"), aspect_error);
        evidence_.insert(QStringLiteral("editedExportPath"), export_path_);
        evidence_.insert(QStringLiteral("editedExportSha256"), sha256(readFile(export_path_)));
        evidence_.insert(QStringLiteral("editedRecipeSha256"), sha256(edited_patch_));
        if (!writeBytes(QDir(output_root_).filePath(QStringLiteral("edited-recipe.json")), edited_patch_)) {
          fail(QStringLiteral("Could not retain the exact reviewed task recipe."));
          break;
        }
        phase_ = Phase::RefreshCatalog;
        catalog_->refresh();
        static_cast<void>(writeCheckpoint(QStringLiteral("catalog_reopen_refresh_requested")));
        break;
      }
      case Phase::RefreshCatalog:
        if (catalog_->loading()) break;
        if (!catalog_->errorMessage().isEmpty()) {
          fail(QStringLiteral("The Catalog could not reopen the saved task recipe: %1").arg(catalog_->errorMessage()));
          break;
        }
        if (catalog_->assetById(options_.asset_id).value(QStringLiteral("adjustmentPatchJson")).toString().toUtf8() != edited_patch_) {
          fail(QStringLiteral("Reopening the imported asset did not return the exact saved revision."));
          break;
        }
        phase_ = Phase::ReopenAsset;
        break;
      case Phase::ReopenAsset: {
        const auto asset = catalog_->assetById(options_.asset_id);
        preview_->loadAsset(options_.asset_id, asset.value(QStringLiteral("filePath")).toString(),
            asset.value(QStringLiteral("thumbnailSource")).toString(), asset.value(QStringLiteral("cameraLabel")).toString(),
            asset.value(QStringLiteral("captureDate")).toString(), asset.value(QStringLiteral("lensModel")).toString(),
            asset.value(QStringLiteral("adjustmentPatchJson")).toString());
        if (preview_->assetId() != options_.asset_id ||
            preview_->currentAssetBoundAdjustmentPatchJson().toUtf8() != edited_patch_) {
          fail(QStringLiteral("The reopened Preview did not load the exact Catalog-saved task revision."));
          break;
        }
        if (!beginExport(QStringLiteral("edited-after-reopen"))) {
          fail(QStringLiteral("The reopened asset export could not start."));
          break;
        }
        phase_ = Phase::ReopenedExport;
        static_cast<void>(writeCheckpoint(QStringLiteral("asset_reopened_from_catalog")));
        break;
      }
      case Phase::ReopenedExport: {
        if (!exportSettled()) break;
        QString error;
        QImage reopened;
        if (!readExport(&reopened, &error)) { fail(error); break; }
        if (reopened != edited_image_ || reopened.colorSpace() != edited_image_.colorSpace()) {
          fail(QStringLiteral("The saved and reopened thumbnail pixels or color profile differ."));
          break;
        }
        evidence_.insert(QStringLiteral("reopenedExportPath"), export_path_);
        evidence_.insert(QStringLiteral("reopenedExportSha256"), sha256(readFile(export_path_)));
        evidence_.insert(QStringLiteral("reopenedPixelsAndProfileMatch"), true);
        const auto capture_result = captureAfterPresentation(
            QStringLiteral("saved-reopened-review"), PresentedCapturePurpose::Review);
        if (capture_result == PresentedCaptureResult::Pending) break;
        if (capture_result == PresentedCaptureResult::Failed) {
          fail(QStringLiteral("The visible saved/reopened Review screen could not be captured: %1")
                   .arg(last_capture_failure_detail_));
          break;
        }
        phase_ = Phase::Revert;
        break;
      }
      case Phase::Revert:
        if (!click(QStringLiteral("agentSessionReviewRevertButton"))) {
          fail(QStringLiteral("The actual Review screen could not invoke whole-task Revert through its visible control."));
          break;
        }
        phase_ = Phase::RevertedExport;
        static_cast<void>(writeCheckpoint(QStringLiteral("whole_task_revert_requested_from_ui")));
        break;
      case Phase::RevertedExport: {
        const auto transaction = edit_->taskTransaction();
        if (transaction.value(QStringLiteral("status")).toString() != QStringLiteral("reverted") ||
            session_->stage() != QStringLiteral("interrupted")) break;
        if (preview_->recipeSavePending() || !catalog_->agentTaskRecipePersistenceSettled(options_.asset_id)) break;
        const auto restored_preview = preview_->currentAssetBoundAdjustmentPatchJson().toUtf8();
        const auto restored_catalog = catalog_->agentTaskRecipeResourceAdapter(options_.asset_id, task_id_).read();
        if (restored_preview != baseline_preview_ || restored_catalog != baseline_catalog_) {
          fail(QStringLiteral("Whole-task Revert did not restore the exact original Preview and Catalog recipe bytes."));
          break;
        }
        if (!beginExport(QStringLiteral("reverted-baseline"))) {
          fail(QStringLiteral("The restored native baseline export could not start."));
          break;
        }
        phase_ = Phase::Done;
        static_cast<void>(writeCheckpoint(QStringLiteral("exact_recipe_reverted_export_started")));
        break;
      }
      case Phase::Done: {
        if (!exportSettled()) break;
        QString error;
        QImage reverted;
        if (!readExport(&reverted, &error)) { fail(error); break; }
        if (reverted != baseline_image_ || reverted.colorSpace() != baseline_image_.colorSpace()) {
          fail(QStringLiteral("Whole-task Revert did not restore the original rendered pixels and color profile."));
          break;
        }
        if (sha256(readFile(options_.source_path)) != source_sha256_) {
          fail(QStringLiteral("The original imported source bytes changed during the task and Revert."));
          break;
        }
        evidence_.insert(QStringLiteral("revertedExportPath"), export_path_);
        evidence_.insert(QStringLiteral("revertedExportSha256"), sha256(readFile(export_path_)));
        evidence_.insert(QStringLiteral("baselinePreviewRecipeRestoredExactly"), true);
        evidence_.insert(QStringLiteral("baselineCatalogRecipeRestoredExactly"), true);
        evidence_.insert(QStringLiteral("revertedPixelsAndProfileMatchBaseline"), true);
        evidence_.insert(QStringLiteral("sourcePreservedByteForByte"), true);
        const auto transaction = edit_->taskTransaction();
        if (transaction.value(QStringLiteral("status")).toString() != QStringLiteral("reverted") ||
            transaction.value(QStringLiteral("taskId")).toString() != task_id_ ||
            session_->stage() != QStringLiteral("interrupted")) {
          fail(QStringLiteral("The visible terminal no longer shows the reverted task state."));
          break;
        }
        const auto restored_preview = preview_->currentAssetBoundAdjustmentPatchJson().toUtf8();
        const auto restored_catalog = catalog_->agentTaskRecipeResourceAdapter(options_.asset_id, task_id_).read();
        if (restored_preview != baseline_preview_ || restored_catalog != baseline_catalog_) {
          fail(QStringLiteral("The exact frozen Preview and Catalog baselines changed before the Revert capture."));
          break;
        }
        const auto capture_result = captureAfterPresentation(
            QStringLiteral("whole-task-reverted"), PresentedCapturePurpose::WholeTaskReverted);
        if (capture_result == PresentedCaptureResult::Pending) break;
        if (capture_result == PresentedCaptureResult::Failed) {
          fail(QStringLiteral("The visible whole-task Revert result could not be captured: %1")
                   .arg(last_capture_failure_detail_));
          break;
        }
        finish(true, {});
        break;
      }
    }
    advancing_ = false;
  }

  void fail(const QString& error) {
    if (finished_) return;
    evidence_.insert(QStringLiteral("status"), QStringLiteral("failed"));
    evidence_.insert(QStringLiteral("failureReason"), error);
    evidence_.insert(QStringLiteral("phase"), static_cast<int>(phase_));
    evidence_.insert(QStringLiteral("taskId"), task_id_);
    if (session_) {
      evidence_.insert(QStringLiteral("sessionStatusAtFailure"), session_->status());
      evidence_.insert(QStringLiteral("sessionPlanAtFailure"), QJsonObject::fromVariantMap(session_->plan()));
    }
    if (edit_) evidence_.insert(QStringLiteral("lastRunReportAtFailure"), QJsonObject::fromVariantMap(edit_->lastRunReport()));
    if (preview_ && preview_->hasAsset()) {
      evidence_.insert(QStringLiteral("previewRecipeSha256AtFailure"),
          sha256(preview_->currentAssetBoundAdjustmentPatchJson().toUtf8()));
      const auto current = preview_->photoCompositionCommand({
          {QStringLiteral("assetId"), options_.asset_id},
          {QStringLiteral("operation"), QStringLiteral("inspect")}});
      if (current.value(QStringLiteral("ok")).toBool())
        evidence_.insert(QStringLiteral("nativeCompositionAtFailure"), QJsonObject::fromVariantMap(current));
    }
    static_cast<void>(writeCheckpoint(QStringLiteral("failed")));
    finish(false, error);
  }

  void finish(bool ok, const QString& error) {
    if (finished_) return;
    finished_ = true;
    timer_.stop();
    evidence_.insert(QStringLiteral("status"), ok ? QStringLiteral("passed") : QStringLiteral("failed"));
    if (ok) evidence_.insert(QStringLiteral("actualApplicationUiQualified"), true);
    if (!error.isEmpty()) evidence_.insert(QStringLiteral("failureReason"), error);
    if (!receipt_path_.isEmpty()) static_cast<void>(writeCheckpoint(ok ? QStringLiteral("complete") : QStringLiteral("failed")));
    if (options_.finished) options_.finished({{QStringLiteral("ok"), ok},
        {QStringLiteral("receiptPath"), receipt_path_}, {QStringLiteral("error"), error},
        {QStringLiteral("receipt"), evidence_.toVariantMap()}});
  }

  QQuickWindow* window_ = nullptr;
  AgentSessionController* session_ = nullptr;
  AgentEditController* edit_ = nullptr;
  EditorPreviewController* preview_ = nullptr;
  CatalogAssetModel* catalog_ = nullptr;
  AgentFullWorkflowProofOptions options_;
  QTimer timer_;
  Phase phase_ = Phase::WaitingForModelProbe;
  QJsonObject evidence_;
  QString receipt_path_;
  QString screenshot_root_;
  QString output_root_;
  QString source_sha256_;
  QString last_capture_failure_detail_;
  QString prior_model_probe_at_utc_;
  QString model_probe_requested_at_utc_;
  qint64 planning_cancel_requested_at_ms_ = 0;
  QString export_path_;
  QString export_name_;
  QString task_id_;
  QByteArray baseline_preview_;
  QByteArray baseline_catalog_;
  QByteArray initial_patch_;
  QByteArray edited_patch_;
  EditRecipeState baseline_state_;
  QVariantMap baseline_composition_;
  QVariantMap initial_run_report_;
  QSet<QString> baseline_layer_ids_;
  QSet<QString> baseline_mask_ids_;
  QImage baseline_image_;
  QImage edited_image_;
  QList<QRect> cover_text_bounds_;
  QList<QVariantMap> cover_text_layers_;
  bool started_ = false;
  bool finished_ = false;
  bool advancing_ = false;
  bool terminal_opened_ = false;
  bool terminal_open_requested_ = false;
  bool first_run_skip_requested_ = false;
  bool asset_edit_activation_requested_ = false;
  bool initial_recipe_persistence_wait_recorded_ = false;
  quint64 presented_frame_serial_ = 0;
  PendingPresentedCapture pending_capture_;
};

}  // namespace photo_editor::desktop

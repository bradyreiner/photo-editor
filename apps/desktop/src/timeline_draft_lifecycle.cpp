#include "timeline_draft_lifecycle.h"

#include "timeline_document_controller.h"
#include "timeline_asset_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QMutexLocker>
#include <QPointer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <QVariant>

#include <algorithm>
#include <cmath>

namespace photo_editor::desktop {
namespace {

constexpr qint64 kMaximumProjectInputBytes = 256LL * 1024LL * 1024LL;
constexpr qsizetype kMaximumProjectInputConfigurationBytes = 64 * 1024;
constexpr int kMaximumProjectInputs = 8;

QVariantMap failure(const QString& error) {
  return {{QStringLiteral("ok"), false},
          {QStringLiteral("mutationNotApplied"), true},
          {QStringLiteral("error"), error}};
}

bool is_number(const QVariant& value) {
  switch (value.metaType().id()) {
    case QMetaType::Double:
    case QMetaType::Float:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
      return true;
    default:
      return false;
  }
}

bool integer_in_range(const QVariant& value, int minimum, int maximum, int* output) {
  if (output == nullptr || !is_number(value)) return false;
  bool converted = false;
  const auto number = value.toDouble(&converted);
  if (!converted || !std::isfinite(number) || std::floor(number) != number ||
      number < static_cast<double>(minimum) || number > static_cast<double>(maximum)) {
    return false;
  }
  *output = static_cast<int>(number);
  return true;
}

bool canonical_uuid(const QString& value) {
  const auto parsed = QUuid(value);
  return !parsed.isNull() && parsed.toString(QUuid::WithoutBraces) == value;
}

bool canonical_sha256(const QString& value) {
  static const QRegularExpression digest_pattern(QStringLiteral("^[0-9a-f]{64}$"));
  return digest_pattern.match(value).hasMatch();
}

QString lowercase_sha256(const QByteArray& bytes) {
  return QString::fromLatin1(
      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool read_project_input_bytes(
    const TimelineDraftLifecycle::ProjectInput& input,
    QByteArray* bytes,
    QString* error) {
  const auto fail_read = [error](const QString& message) {
    if (error != nullptr) *error = message;
    return false;
  };
  const QFileInfo before(input.canonical_path);
  if (before.isSymLink() || !before.isFile() || !before.isReadable() ||
      before.canonicalFilePath() != input.canonical_path ||
      before.size() < 1 || before.size() > kMaximumProjectInputBytes ||
      before.size() != input.byte_size) {
    return fail_read(QStringLiteral("The granted project input is no longer available in its verified form."));
  }
  QFile file(input.canonical_path);
  if (!file.open(QIODevice::ReadOnly)) {
    return fail_read(QStringLiteral("The granted project input could not be read."));
  }
  const auto data = file.readAll();
  if (file.error() != QFileDevice::NoError || data.size() != before.size()) {
    return fail_read(QStringLiteral("The granted project input could not be read completely."));
  }
  const QFileInfo after(input.canonical_path);
  if (after.isSymLink() || after.canonicalFilePath() != input.canonical_path ||
      after.size() != before.size() || lowercase_sha256(data) != input.sha256) {
    return fail_read(QStringLiteral("The granted project input changed after startup validation."));
  }
  if (bytes != nullptr) *bytes = data;
  return true;
}

bool parse_project_input_configuration(
    const QByteArray& configuration,
    QVector<TimelineDraftLifecycle::ProjectInput>* inputs,
    QString* error) {
  if (inputs == nullptr) return false;
  inputs->clear();
  if (configuration.trimmed().isEmpty()) return true;
  const auto invalid = [inputs, error]() {
    inputs->clear();
    if (error != nullptr) {
      *error = QStringLiteral(
          "Startup project inputs are unavailable because their configuration failed validation.");
    }
    return false;
  };
  if (configuration.size() > kMaximumProjectInputConfigurationBytes) return invalid();
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(configuration, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isArray()) return invalid();
  const auto values = document.array();
  if (values.size() > kMaximumProjectInputs) return invalid();
  static const QRegularExpression digest_pattern(QStringLiteral("^[0-9a-f]{64}$"));
  QSet<QString> seen_paths;
  QVector<TimelineDraftLifecycle::ProjectInput> parsed;
  parsed.reserve(values.size());
  for (const auto& value : values) {
    if (!value.isObject()) return invalid();
    const auto object = value.toObject();
    const auto keys = object.keys();
    if (keys != QStringList{QStringLiteral("path"), QStringLiteral("sha256")}) return invalid();
    const auto path = object.value(QStringLiteral("path")).toString().trimmed();
    const auto digest = object.value(QStringLiteral("sha256")).toString();
    if (path.isEmpty() || !digest_pattern.match(digest).hasMatch()) return invalid();
    const QFileInfo info(path);
    const auto canonical = info.canonicalFilePath();
    const auto absolute = QDir::cleanPath(info.absoluteFilePath());
    if (info.isSymLink() || !info.isFile() || !info.isReadable() ||
        canonical.isEmpty() || canonical != absolute ||
        info.size() < 1 || info.size() > kMaximumProjectInputBytes ||
        seen_paths.contains(canonical)) {
      return invalid();
    }
    TimelineDraftLifecycle::ProjectInput input;
    input.input_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    input.display_name = info.completeBaseName().trimmed().left(120);
    if (input.display_name.isEmpty()) input.display_name = info.fileName().left(120);
    input.canonical_path = canonical;
    input.sha256 = digest;
    input.byte_size = info.size();
    QString read_error;
    if (!read_project_input_bytes(input, nullptr, &read_error)) return invalid();
    parsed.push_back(std::move(input));
    seen_paths.insert(canonical);
  }
  *inputs = std::move(parsed);
  return true;
}

struct SavedDraft final {
  QString timeline_id;
  QString path;
  QString document_hash;
  QVariantMap summary;
};

bool validated_saved_draft(
    const QVariantMap& record,
    SavedDraft* draft,
    QString* error,
    bool include_document_hash = false) {
  const auto timeline_id = record.value(QStringLiteral("timelineId")).toString();
  if (!canonical_uuid(timeline_id)) {
    if (error != nullptr) *error = QStringLiteral("The Library record has an invalid Timeline ID.");
    return false;
  }

  const auto recorded_path = record.value(QStringLiteral("documentPath")).toString();
  const QFileInfo file_info(recorded_path);
  const auto canonical_path = file_info.canonicalFilePath();
  const auto canonical_documents_root = QFileInfo(timelineDocumentsRootPath()).canonicalFilePath();
  const auto expected_path = canonical_documents_root.isEmpty()
      ? QString{} : QDir(canonical_documents_root).filePath(timeline_id + QStringLiteral(".json"));
  if (recorded_path.trimmed().isEmpty() || file_info.isSymLink() || !file_info.isFile() ||
      !file_info.isReadable() || canonical_path.isEmpty() || canonical_path != expected_path ||
      file_info.fileName() != timeline_id + QStringLiteral(".json")) {
    if (error != nullptr) *error = QStringLiteral("The saved Timeline path is missing, noncanonical, or outside its managed identity.");
    return false;
  }
  const auto parent_path = QFileInfo(canonical_path).dir().canonicalPath();
  if (parent_path.isEmpty() || parent_path != canonical_documents_root ||
      QDir(parent_path).filePath(file_info.fileName()) != canonical_path) {
    if (error != nullptr) *error = QStringLiteral("The saved Timeline path has a noncanonical parent directory.");
    return false;
  }

  QFile file(canonical_path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error != nullptr) *error = QStringLiteral("The saved Timeline document cannot be read.");
    return false;
  }
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(file.readAll(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    if (error != nullptr) *error = QStringLiteral("The saved Timeline is not a readable project document.");
    return false;
  }
  const auto object = document.object();
  if (object.value(QStringLiteral("timelineId")).toString() != timeline_id ||
      !object.value(QStringLiteral("sequenceFormat")).isObject() ||
      !object.value(QStringLiteral("tracks")).isArray()) {
    if (error != nullptr) *error = QStringLiteral("The saved Timeline identity or document structure does not match its Library record.");
    return false;
  }

  if (draft != nullptr) {
    draft->timeline_id = timeline_id;
    draft->path = canonical_path;
    if (include_document_hash) {
      auto stable_document = object;
      stable_document.remove(QStringLiteral("updatedAt"));
      draft->document_hash = lowercase_sha256(
          QJsonDocument(stable_document).toJson(QJsonDocument::Compact));
    }
    draft->summary = {
        {QStringLiteral("timelineId"), timeline_id},
        {QStringLiteral("name"), record.value(QStringLiteral("name")).toString()},
        {QStringLiteral("createdAt"), record.value(QStringLiteral("createdAt"))},
        {QStringLiteral("updatedAt"), record.value(QStringLiteral("updatedAt"))},
        {QStringLiteral("durationSeconds"), record.value(QStringLiteral("durationSeconds"))},
    };
  }
  return true;
}

QVariantMap navigation_receipt(
    const QVariantMap& inspected,
    const QString& operation) {
  const auto lost_reply_resolution = operation == QStringLiteral("created")
      ? QStringLiteral("Call timeline.inspectAgentDocument and timeline.list_saved_drafts; use the returned resourceId and do not replay project creation.")
      : QStringLiteral("Call timeline.inspectAgentDocument and continue from its resourceId; do not replay this navigation command.");
  return {{QStringLiteral("ok"), true},
          {QStringLiteral("operation"), operation},
          {QStringLiteral("resource"), inspected},
          {QStringLiteral("resourceId"), inspected.value(QStringLiteral("resourceId"))},
          {QStringLiteral("navigationRevertible"), false},
          {QStringLiteral("replaySafe"), false},
          {QStringLiteral("lostReplyResolution"), lost_reply_resolution}};
}

}  // namespace

TimelineDraftLifecycle::TimelineDraftLifecycle(
    TimelineDocumentController* timeline,
    Policy task_active,
    Policy edit_access,
    Policy activate_timeline_workspace,
    QByteArray project_input_configuration)
    : timeline_(timeline),
      task_active_(std::move(task_active)),
      edit_access_(std::move(edit_access)),
      activate_timeline_workspace_(std::move(activate_timeline_workspace)) {
  autosave_recovery_state_ = std::make_shared<AutosaveRecoveryState>();
  if (timeline_ != nullptr) {
    const auto state = autosave_recovery_state_;
    const QPointer<TimelineDocumentController> timeline_guard(timeline_);
    const auto activate_workspace = activate_timeline_workspace_;
    QObject::connect(
        timeline_, &TimelineDocumentController::recoveryOpenCompleted, timeline_,
        [state, timeline_guard, activate_workspace](const QString& candidate_id, bool succeeded) {
          QString timeline_id;
          {
            QMutexLocker lock(&state->mutex);
            if (state->candidate_id != candidate_id || !state->accepted) return;
            timeline_id = state->timeline_id;
          }

          auto workspace_activated = false;
          if (succeeded && timeline_guard != nullptr) {
            const auto expected_resource_id = QStringLiteral("timeline:") + timeline_id;
            const auto resource_matches = timeline_guard->hasActiveTimeline() &&
                timeline_guard->agentTaskDocumentId() == expected_resource_id;
            if (resource_matches && activate_workspace) workspace_activated = activate_workspace();
          }

          QMutexLocker lock(&state->mutex);
          state->terminal = true;
          state->succeeded = succeeded;
          state->workspace_activated = workspace_activated;
        });
  }
  (void)parse_project_input_configuration(
      project_input_configuration, &project_inputs_, &project_input_configuration_error_);
}

QVariantMap TimelineDraftLifecycle::invoke(
    const QString& action,
    const QVariantMap& arguments) const {
  if (timeline_ == nullptr) return failure(QStringLiteral("The native Timeline controller is unavailable."));
  if (action == QStringLiteral("list_saved_drafts")) {
    if (!arguments.isEmpty()) return failure(QStringLiteral("Listing saved Timelines accepts no arguments."));
    return listSavedDrafts();
  }
  if (action == QStringLiteral("inspect_document")) {
    if ((!arguments.isEmpty() && arguments.keys() != QStringList{QStringLiteral("includeDocumentHash")}) ||
        (arguments.contains(QStringLiteral("includeDocumentHash")) &&
         arguments.value(QStringLiteral("includeDocumentHash")).metaType().id() != QMetaType::Bool)) {
      return failure(QStringLiteral("Inspecting the active Timeline accepts only an optional includeDocumentHash boolean."));
    }
    return inspectDocument(arguments.value(QStringLiteral("includeDocumentHash"), true).toBool());
  }
  if (action == QStringLiteral("list_project_inputs")) {
    if (!arguments.isEmpty()) return failure(QStringLiteral("Listing granted project inputs accepts no arguments."));
    return listProjectInputs();
  }
  if (action == QStringLiteral("import_project_input")) return importProjectInput(arguments);
  if (action == QStringLiteral("save_active_draft")) return saveActiveDraft(arguments);
  if (action == QStringLiteral("close_saved_draft")) return closeSavedDraft(arguments);
  if (action == QStringLiteral("create_draft")) return createDraft(arguments);
  if (action == QStringLiteral("open_saved_draft")) return openSavedDraft(arguments);
  if (action == QStringLiteral("open_autosave_recovery")) return openAutosaveRecovery(arguments);
  if (action == QStringLiteral("inspect_autosave_recovery")) return inspectAutosaveRecovery(arguments);
  return failure(QStringLiteral("Unknown Timeline draft lifecycle operation."));
}

QVariantMap TimelineDraftLifecycle::listSavedDrafts() const {
  QVariantList drafts;
  QSet<QString> seen;
  int rejected_records = 0;
  const auto records = timeline_->recentTimelines();
  for (const auto& value : records) {
    const auto record = value.toMap();
    SavedDraft draft;
    if (!validated_saved_draft(record, &draft, nullptr) || seen.contains(draft.timeline_id)) {
      ++rejected_records;
      continue;
    }
    seen.insert(draft.timeline_id);
    drafts.append(draft.summary);
  }
  return {{QStringLiteral("ok"), true},
          {QStringLiteral("drafts"), drafts},
          {QStringLiteral("draftCount"), drafts.size()},
          {QStringLiteral("unavailableRecordCount"), rejected_records}};
}

QVariantMap TimelineDraftLifecycle::inspectDocument(bool include_document_hash) const {
  const auto active = timeline_->hasActiveTimeline();
  const auto ready = active && timeline_->agentTaskDocumentReady();
  const auto export_snapshot_readiness = timeline_->agentExportSnapshotReadiness();
  const auto saved_path = active ? timeline_->savedDocumentPath() : QString{};
  const auto saved_info = QFileInfo(saved_path);
  QVariantMap result{
      {QStringLiteral("ok"), true},
      {QStringLiteral("contract"), QStringLiteral("looksy.native-timeline-inspection/v1")},
      {QStringLiteral("active"), active},
      {QStringLiteral("resourceId"), active ? timeline_->agentTaskDocumentId() : QString{}},
      {QStringLiteral("ready"), ready},
      {QStringLiteral("exportSnapshotReady"),
       active && export_snapshot_readiness.value(QStringLiteral("ready")).toBool()},
      {QStringLiteral("exportSnapshotReadiness"), export_snapshot_readiness},
      {QStringLiteral("saved"), active && saved_info.isFile()},
      {QStringLiteral("dirty"), active && timeline_->dirty()},
      {QStringLiteral("saveInProgress"), active && timeline_->timelineSaveInProgress()},
      {QStringLiteral("saveFailed"), active && timeline_->timelineSaveFailed()},
      {QStringLiteral("width"), active ? timeline_->sequenceWidth() : 0},
      {QStringLiteral("height"), active ? timeline_->sequenceHeight() : 0},
      {QStringLiteral("frameRate"), active ? timeline_->framesPerSecond() : 0.0},
      {QStringLiteral("durationSeconds"), active ? timeline_->totalDurationSecondsValue() : 0.0},
      {QStringLiteral("trackCount"), active ? timeline_->trackCount() : 0},
      {QStringLiteral("documentHash"), QString{}},
      {QStringLiteral("documentHashIncluded"), false},
  };
  if (include_document_hash && (ready || result.value(QStringLiteral("exportSnapshotReady")).toBool())) {
    result.insert(QStringLiteral("documentHash"), timeline_->deterministicDocumentHash());
    result.insert(QStringLiteral("documentHashIncluded"), true);
  }
  return result;
}

QVariantMap TimelineDraftLifecycle::listProjectInputs() const {
  QVariantList inputs;
  inputs.reserve(project_inputs_.size());
  for (const auto& input : project_inputs_) {
    inputs.push_back(QVariantMap{
        {QStringLiteral("inputId"), input.input_id},
        {QStringLiteral("displayName"), input.display_name}});
  }
  QVariantMap result{
      {QStringLiteral("ok"), true},
      {QStringLiteral("inputs"), inputs},
      {QStringLiteral("inputCount"), inputs.size()},
      {QStringLiteral("configurationValid"), project_input_configuration_error_.isEmpty()}};
  if (!project_input_configuration_error_.isEmpty()) {
    result.insert(QStringLiteral("configurationError"), project_input_configuration_error_);
  }
  return result;
}

QVariantMap TimelineDraftLifecycle::importProjectInput(const QVariantMap& arguments) const {
  if (timeline_->hasActiveTimeline()) {
    return failure(QStringLiteral("Import a project only while the Timeline is inactive."));
  }
  if (task_active_ && task_active_()) {
    return failure(QStringLiteral("Finish, release, or revert the active or review-held task before importing a Timeline."));
  }
  if (edit_access_ && !edit_access_()) {
    return failure(QStringLiteral("Importing a Timeline project requires an active trial or purchase."));
  }
  if (arguments.keys() != QStringList{QStringLiteral("confirmed"), QStringLiteral("inputId")} ||
      arguments.value(QStringLiteral("inputId")).metaType().id() != QMetaType::QString ||
      arguments.value(QStringLiteral("confirmed")).metaType().id() != QMetaType::Bool ||
      !arguments.value(QStringLiteral("confirmed")).toBool()) {
    return failure(QStringLiteral("Import requires an exact granted inputId and confirmed:true."));
  }
  const auto input_id = arguments.value(QStringLiteral("inputId")).toString();
  const auto found = std::find_if(
      project_inputs_.cbegin(), project_inputs_.cend(),
      [&input_id](const ProjectInput& input) { return input.input_id == input_id; });
  if (found == project_inputs_.cend()) {
    return failure(project_input_configuration_error_.isEmpty()
        ? QStringLiteral("That project input ID was not granted at startup.")
        : project_input_configuration_error_);
  }
  QByteArray project_bytes;
  QString read_error;
  if (!read_project_input_bytes(*found, &project_bytes, &read_error)) {
    return failure(read_error);
  }
  auto result = timeline_->importTimelineProjectCopyForAutomation(project_bytes);
  result.insert(QStringLiteral("operation"), QStringLiteral("imported_copy"));
  result.insert(QStringLiteral("inputId"), found->input_id);
  result.insert(QStringLiteral("displayName"), found->display_name);
  result.insert(QStringLiteral("sourceSha256"), found->sha256);
  result.insert(QStringLiteral("sourceByteSize"), found->byte_size);
  result.insert(QStringLiteral("navigationRevertible"), false);
  result.insert(QStringLiteral("replaySafe"), false);
  result.insert(
      QStringLiteral("lostReplyResolution"),
      QStringLiteral("Call timeline.inspectAgentDocument and timeline.list_saved_drafts; inspect the returned resource and do not replay project import."));
  if (!result.value(QStringLiteral("ok")).toBool() &&
      !result.value(QStringLiteral("mutationApplied")).toBool()) {
    return result;
  }
  return activateAfterNavigation(std::move(result), QStringLiteral("imported_copy"));
}

QVariantMap TimelineDraftLifecycle::saveActiveDraft(const QVariantMap& arguments) const {
  if (!timeline_->hasActiveTimeline()) {
    return failure(QStringLiteral("Open a Timeline before saving the active draft."));
  }
  if (task_active_ && task_active_()) {
    return failure(QStringLiteral("Finish, release, or revert the active or review-held task before saving the draft."));
  }
  if (edit_access_ && !edit_access_()) {
    return failure(QStringLiteral("Saving a Timeline draft requires an active trial or purchase."));
  }
  if (arguments.keys() != QStringList{QStringLiteral("confirmed"),
                                      QStringLiteral("documentHash"),
                                      QStringLiteral("resourceId")} ||
      arguments.value(QStringLiteral("confirmed")).metaType().id() != QMetaType::Bool ||
      !arguments.value(QStringLiteral("confirmed")).toBool() ||
      arguments.value(QStringLiteral("documentHash")).metaType().id() != QMetaType::QString ||
      arguments.value(QStringLiteral("resourceId")).metaType().id() != QMetaType::QString) {
    return failure(QStringLiteral("Saving requires only the exact resourceId, stable documentHash, and confirmed:true."));
  }

  const auto resource_id = arguments.value(QStringLiteral("resourceId")).toString();
  const auto document_hash = arguments.value(QStringLiteral("documentHash")).toString();
  const auto timeline_id = resource_id.mid(QStringLiteral("timeline:").size());
  if (!canonical_uuid(timeline_id) || resource_id != QStringLiteral("timeline:") + timeline_id ||
      !canonical_sha256(document_hash)) {
    return failure(QStringLiteral("Saving requires the canonical active Timeline resource ID and lowercase stable SHA-256."));
  }
  if (timeline_->agentTaskDocumentId() != resource_id ||
      timeline_->deterministicDocumentHash() != document_hash) {
    return failure(QStringLiteral("The active Timeline identity or content changed; inspect it again before saving."));
  }
  if (timeline_->timelineSaveInProgress()) {
    return failure(QStringLiteral("A Timeline save is already in progress; inspect saveInProgress before retrying."));
  }
  if (!timeline_->saveTimelineAsync(false)) {
    auto result = failure(QStringLiteral("Looksy could not start saving the active Timeline draft."));
    result.insert(QStringLiteral("saveInProgress"), timeline_->timelineSaveInProgress());
    result.insert(QStringLiteral("saveFailed"), timeline_->timelineSaveFailed());
    result.insert(QStringLiteral("dirty"), timeline_->dirty());
    return result;
  }
  return {{QStringLiteral("ok"), true},
          {QStringLiteral("accepted"), true},
          {QStringLiteral("operation"), QStringLiteral("saving_active_draft")},
          {QStringLiteral("resourceId"), resource_id},
          {QStringLiteral("documentHash"), document_hash},
          {QStringLiteral("saveInProgress"), timeline_->timelineSaveInProgress()},
          {QStringLiteral("saveFailed"), timeline_->timelineSaveFailed()},
          {QStringLiteral("dirty"), timeline_->dirty()},
          {QStringLiteral("replaySafe"), false}};
}

QVariantMap TimelineDraftLifecycle::closeSavedDraft(const QVariantMap& arguments) const {
  if (!timeline_->hasActiveTimeline()) {
    return failure(QStringLiteral("There is no active Timeline draft to close."));
  }
  if (task_active_ && task_active_()) {
    return failure(QStringLiteral("Finish, release, or revert the active or review-held task before closing the draft."));
  }
  if (arguments.keys() != QStringList{QStringLiteral("confirmed"),
                                      QStringLiteral("documentHash"),
                                      QStringLiteral("resourceId")} ||
      arguments.value(QStringLiteral("confirmed")).metaType().id() != QMetaType::Bool ||
      !arguments.value(QStringLiteral("confirmed")).toBool() ||
      arguments.value(QStringLiteral("documentHash")).metaType().id() != QMetaType::QString ||
      arguments.value(QStringLiteral("resourceId")).metaType().id() != QMetaType::QString) {
    return failure(QStringLiteral("Closing requires only the exact resourceId, stable documentHash, and confirmed:true."));
  }

  const auto resource_id = arguments.value(QStringLiteral("resourceId")).toString();
  const auto document_hash = arguments.value(QStringLiteral("documentHash")).toString();
  const auto timeline_id = resource_id.mid(QStringLiteral("timeline:").size());
  if (!canonical_uuid(timeline_id) || resource_id != QStringLiteral("timeline:") + timeline_id ||
      !canonical_sha256(document_hash)) {
    return failure(QStringLiteral("Closing requires the canonical active Timeline resource ID and lowercase stable SHA-256."));
  }
  if (timeline_->agentTaskDocumentId() != resource_id ||
      timeline_->deterministicDocumentHash() != document_hash) {
    return failure(QStringLiteral("The active Timeline identity or content changed; inspect it again before closing."));
  }
  if (timeline_->timelineSaveInProgress() || timeline_->timelineSaveFailed() || timeline_->dirty()) {
    return failure(QStringLiteral("The Timeline must finish saving successfully and be clean before it can close."));
  }
  SavedDraft match;
  int matches = 0;
  for (const auto& value : timeline_->recentTimelines()) {
    const auto record = value.toMap();
    if (record.value(QStringLiteral("timelineId")).toString() != timeline_id) continue;
    ++matches;
    QString validation_error;
    if (!validated_saved_draft(record, &match, &validation_error, true)) {
      return failure(validation_error);
    }
  }
  if (matches != 1 || match.path.isEmpty()) {
    return failure(matches == 0
        ? QStringLiteral("The active Timeline has no verified app-managed Library record.")
        : QStringLiteral("The Library contains an ambiguous duplicate Timeline ID."));
  }
  const auto saved_path = timeline_->savedDocumentPath();
  const QFileInfo active_path_info(saved_path);
  if (active_path_info.isSymLink() || active_path_info.canonicalFilePath() != match.path ||
      match.document_hash != document_hash) {
    return failure(QStringLiteral("The canonical Library file does not read back as the exact active Timeline document."));
  }

  timeline_->closeTimeline();
  if (timeline_->hasActiveTimeline()) {
    return failure(QStringLiteral("Looksy did not close the verified saved Timeline; inspect the active document before retrying."));
  }
  return {{QStringLiteral("ok"), true},
          {QStringLiteral("closed"), true},
          {QStringLiteral("operation"), QStringLiteral("closed_saved_draft")},
          {QStringLiteral("resourceId"), resource_id},
          {QStringLiteral("timelineId"), timeline_id},
          {QStringLiteral("documentHash"), document_hash},
          {QStringLiteral("replaySafe"), false}};
}

QVariantMap TimelineDraftLifecycle::createDraft(const QVariantMap& arguments) const {
  if (timeline_->hasActiveTimeline()) {
    return failure(QStringLiteral("Close or save the current Timeline in the app before creating another draft."));
  }
  if (task_active_ && task_active_()) {
    return failure(QStringLiteral("Finish, release, or revert the active or review-held task before navigating to a new Timeline."));
  }
  if (edit_access_ && !edit_access_()) {
    return failure(QStringLiteral("Creating a Timeline draft requires an active trial or purchase."));
  }
  if (arguments.keys() != QStringList{QStringLiteral("settings")}) {
    return failure(QStringLiteral("Create requires only a validated settings object."));
  }
  const auto settings = arguments.value(QStringLiteral("settings")).toMap();
  static const QSet<QString> allowed_settings{
      QStringLiteral("timelineName"), QStringLiteral("width"), QStringLiteral("height"),
      QStringLiteral("frameRate"), QStringLiteral("audioSampleRate"),
      QStringLiteral("colorSpace"), QStringLiteral("dataLevels"),
      QStringLiteral("trackLayout")};
  for (auto iterator = settings.cbegin(); iterator != settings.cend(); ++iterator) {
    if (!allowed_settings.contains(iterator.key())) {
      return failure(QStringLiteral("Timeline settings contain an unsupported field."));
    }
  }
  const auto name = settings.value(QStringLiteral("timelineName")).toString().trimmed();
  int width = 0;
  int height = 0;
  int audio_sample_rate = 48000;
  const auto rate_value = settings.value(QStringLiteral("frameRate"));
  bool rate_converted = false;
  const auto frame_rate = rate_value.toDouble(&rate_converted);
  if (name.isEmpty() || name.size() > 120 ||
      !integer_in_range(settings.value(QStringLiteral("width")), 16, 16384, &width) ||
      !integer_in_range(settings.value(QStringLiteral("height")), 16, 16384, &height) ||
      !is_number(rate_value) || !rate_converted || !std::isfinite(frame_rate) ||
      frame_rate < 1.0 || frame_rate > 240.0 ||
      (settings.contains(QStringLiteral("audioSampleRate")) &&
       !integer_in_range(settings.value(QStringLiteral("audioSampleRate")), 8000, 192000,
                         &audio_sample_rate))) {
    return failure(QStringLiteral("Draft settings require a 1–120 character name, integer dimensions from 16 to 16384, frame rate from 1 to 240, and an optional audio rate from 8000 to 192000."));
  }
  const auto color_space = settings.value(QStringLiteral("colorSpace"), QStringLiteral("Rec.709")).toString();
  const auto canonical_color_space = [&color_space]() {
    for (const auto& candidate : {QStringLiteral("Rec.709"), QStringLiteral("Rec.2020"),
                                  QStringLiteral("Display P3")}) {
      if (candidate.compare(color_space, Qt::CaseInsensitive) == 0) return candidate;
    }
    return QString{};
  }();
  if (canonical_color_space.isEmpty()) {
    return failure(QStringLiteral("Color space must be Rec.709, Rec.2020, or Display P3."));
  }
  const auto data_levels = settings.value(QStringLiteral("dataLevels"), QStringLiteral("Legal")).toString();
  const auto canonical_data_levels = data_levels.compare(QStringLiteral("Full"), Qt::CaseInsensitive) == 0
      ? QStringLiteral("Full")
      : data_levels.compare(QStringLiteral("Legal"), Qt::CaseInsensitive) == 0
          ? QStringLiteral("Legal") : QString{};
  if (canonical_data_levels.isEmpty()) {
    return failure(QStringLiteral("Data levels must be Legal or Full."));
  }
  const auto track_layout = settings.value(QStringLiteral("trackLayout"), QStringLiteral("standard")).toString();
  if (!QStringList{QStringLiteral("standard"), QStringLiteral("creator_social")}.contains(track_layout)) {
    return failure(QStringLiteral("Track layout must be standard or creator_social."));
  }

  QVariantMap normalized_settings{
      {QStringLiteral("timelineName"), name},
      {QStringLiteral("width"), width},
      {QStringLiteral("height"), height},
      {QStringLiteral("frameRate"), frame_rate},
      {QStringLiteral("audioSampleRate"), audio_sample_rate},
      {QStringLiteral("colorSpace"), canonical_color_space},
      {QStringLiteral("dataLevels"), canonical_data_levels},
      {QStringLiteral("trackLayout"), track_layout},
  };
  timeline_->createTimelineWithSettings(normalized_settings);
  const auto inspected = timeline_->inspectAgentDocument();
  const auto timeline_id = timeline_->agentTaskDocumentId().mid(QStringLiteral("timeline:").size());
  if (!timeline_->hasActiveTimeline() || !canonical_uuid(timeline_id) ||
      !inspected.value(QStringLiteral("ready")).toBool() ||
      !timeline_->savedDocumentPath().isEmpty() || timeline_->dirty() ||
      inspected.value(QStringLiteral("width")).toInt() != width ||
      inspected.value(QStringLiteral("height")).toInt() != height ||
      qAbs(inspected.value(QStringLiteral("frameRate")).toDouble() - frame_rate) > 0.000001) {
    auto result = failure(QStringLiteral("Looksy did not create the requested empty native Timeline draft."));
    result.insert(QStringLiteral("mutationNotApplied"), false);
    result.insert(QStringLiteral("mutationApplied"), timeline_->hasActiveTimeline());
    result.insert(QStringLiteral("resource"), inspected);
    return result;
  }

  // Establish the durable Library project before task admission. This is a
  // fresh UUID from createTimelineWithSettings; verify its exact managed path
  // is absent before the native save can write it.
  if (!QDir().mkpath(timelineDocumentsRootPath())) {
    auto result = failure(QStringLiteral("The managed Timeline Library folder is unavailable."));
    result.insert(QStringLiteral("mutationNotApplied"), false);
    result.insert(QStringLiteral("mutationApplied"), true);
    result.insert(QStringLiteral("resource"), timeline_->inspectAgentDocument());
    return result;
  }
  const auto documents_root = QFileInfo(timelineDocumentsRootPath()).canonicalFilePath();
  const auto expected_path = documents_root.isEmpty()
      ? QString{} : QDir(documents_root).filePath(timeline_id + QStringLiteral(".json"));
  const QFileInfo expected_info(expected_path);
  const auto records_before_save = timeline_->recentTimelines();
  const auto id_already_registered = std::any_of(
      records_before_save.cbegin(), records_before_save.cend(),
      [&timeline_id](const QVariant& value) {
        return value.toMap().value(QStringLiteral("timelineId")).toString() == timeline_id;
      });
  if (documents_root.isEmpty() || expected_path.isEmpty() ||
      expected_info.exists() || expected_info.isSymLink() || id_already_registered ||
      !timeline_->savedDocumentPath().isEmpty()) {
    auto result = failure(QStringLiteral("The new Timeline could not reserve a fresh managed Library document path."));
    result.insert(QStringLiteral("mutationNotApplied"), false);
    result.insert(QStringLiteral("mutationApplied"), true);
    result.insert(QStringLiteral("resource"), timeline_->inspectAgentDocument());
    return result;
  }

  if (!timeline_->saveTimeline(false)) {
    auto result = failure(QStringLiteral("The empty Timeline draft was created, but Looksy could not save it to the Library."));
    result.insert(QStringLiteral("mutationNotApplied"), false);
    result.insert(QStringLiteral("mutationApplied"), true);
    result.insert(QStringLiteral("resource"), timeline_->inspectAgentDocument());
    return result;
  }

  const auto saved_path = QFileInfo(timeline_->savedDocumentPath()).canonicalFilePath();
  bool library_recorded = false;
  for (const auto& value : timeline_->recentTimelines()) {
    SavedDraft saved_draft;
    if (value.toMap().value(QStringLiteral("timelineId")).toString() == timeline_id &&
        validated_saved_draft(value.toMap(), &saved_draft, nullptr) &&
        saved_draft.path == expected_path) {
      library_recorded = true;
      break;
    }
  }
  const auto saved_resource = timeline_->inspectAgentDocument();
  if (saved_path != expected_path || !library_recorded ||
      saved_resource.value(QStringLiteral("resourceId")).toString() != QStringLiteral("timeline:") + timeline_id) {
    auto result = failure(QStringLiteral("The new Timeline exists, but its canonical Library identity could not be verified."));
    result.insert(QStringLiteral("mutationNotApplied"), false);
    result.insert(QStringLiteral("mutationApplied"), true);
    result.insert(QStringLiteral("resource"), saved_resource);
    return result;
  }
  auto receipt = navigation_receipt(saved_resource, QStringLiteral("created"));
  receipt.insert(QStringLiteral("projectFileCreated"), true);
  receipt.insert(QStringLiteral("libraryRegistered"), true);
  return activateAfterNavigation(std::move(receipt), QStringLiteral("created"));
}

QVariantMap TimelineDraftLifecycle::openSavedDraft(const QVariantMap& arguments) const {
  if (timeline_->hasActiveTimeline()) {
    return failure(QStringLiteral("Close or save the current Timeline in the app before opening a saved draft."));
  }
  if (task_active_ && task_active_()) {
    return failure(QStringLiteral("Finish, release, or revert the active or review-held task before navigating to a saved Timeline."));
  }
  if (edit_access_ && !edit_access_()) {
    return failure(QStringLiteral("Opening a saved Timeline draft requires an active trial or purchase."));
  }
  if (arguments.keys() != QStringList{QStringLiteral("timelineId")}) {
    return failure(QStringLiteral("Open requires only a saved Library Timeline ID; filesystem paths are not accepted."));
  }
  const auto timeline_id = arguments.value(QStringLiteral("timelineId")).toString();
  if (!canonical_uuid(timeline_id)) {
    return failure(QStringLiteral("Provide the canonical Timeline ID returned by timeline.list_saved_drafts."));
  }
  SavedDraft match;
  int matches = 0;
  for (const auto& value : timeline_->recentTimelines()) {
    const auto record = value.toMap();
    if (record.value(QStringLiteral("timelineId")).toString() != timeline_id) continue;
    ++matches;
    QString validation_error;
    if (!validated_saved_draft(record, &match, &validation_error)) {
      return failure(validation_error);
    }
  }
  if (matches != 1 || match.path.isEmpty()) {
    return failure(matches == 0
        ? QStringLiteral("That Timeline ID is not present in the saved Library drafts.")
        : QStringLiteral("The saved Library contains an ambiguous duplicate Timeline ID."));
  }

  // The file was opened and its exact ID was checked before calling the
  // controller. This excludes loadTimeline's recovery-backup fallback path.
  if (!timeline_->loadTimeline(match.path)) {
    auto result = failure(QStringLiteral("Looksy could not open the verified saved Timeline document."));
    result.insert(QStringLiteral("mutationNotApplied"), false);
    result.insert(QStringLiteral("mutationApplied"), timeline_->hasActiveTimeline());
    result.insert(QStringLiteral("resource"), timeline_->inspectAgentDocument());
    return result;
  }
  // Opening a project is navigation, not a request for every clip/keyframe.
  // Large edits otherwise produce hundred-megabyte replies on the GUI thread.
  auto inspected = inspectDocument();
  inspected.insert(QStringLiteral("savedDocumentPath"), timeline_->savedDocumentPath());
  inspected.insert(QStringLiteral("name"), timeline_->timelineName());
  if (timeline_->agentTaskDocumentId() != QStringLiteral("timeline:") + timeline_id ||
      QFileInfo(timeline_->savedDocumentPath()).canonicalFilePath() != match.path) {
    auto result = failure(QStringLiteral("The opened Timeline did not retain the verified Library identity."));
    result.insert(QStringLiteral("mutationNotApplied"), false);
    result.insert(QStringLiteral("mutationApplied"), true);
    result.insert(QStringLiteral("resource"), inspected);
    return result;
  }
  return activateAfterNavigation(navigation_receipt(inspected, QStringLiteral("opened")),
                                QStringLiteral("opened"));
}

QVariantMap TimelineDraftLifecycle::openAutosaveRecovery(const QVariantMap& arguments) const {
  if (timeline_->hasActiveTimeline()) {
    return failure(QStringLiteral("Close or save the current Timeline before opening an autosave recovery."));
  }
  if (task_active_ && task_active_()) {
    return failure(QStringLiteral("Finish, release, or revert the active or review-held task before opening a recovery."));
  }
  if (edit_access_ && !edit_access_()) {
    return failure(QStringLiteral("Opening Timeline recovery requires an active trial or purchase."));
  }
  if (arguments.keys() != QStringList{QStringLiteral("documentHash"), QStringLiteral("timelineId")}) {
    return failure(QStringLiteral("Autosave recovery requires only timelineId and documentHash; paths are not accepted."));
  }
  const auto timeline_id = arguments.value(QStringLiteral("timelineId")).toString();
  const auto document_hash = arguments.value(QStringLiteral("documentHash")).toString();
  if (!canonical_uuid(timeline_id) || !canonical_sha256(document_hash)) {
    return failure(QStringLiteral("Provide a canonical Timeline ID and lowercase stable document SHA-256."));
  }
  if (timeline_->recoveryOpenInProgress()) {
    return failure(QStringLiteral("A Timeline recovery is already opening."));
  }

  const auto candidate_id = QStringLiteral("autosave:%1:%2").arg(timeline_id, document_hash);
  {
    QMutexLocker lock(&autosave_recovery_state_->mutex);
    if (autosave_recovery_state_->accepted && !autosave_recovery_state_->terminal) {
      return failure(QStringLiteral("A prior autosave recovery request is still unresolved; inspect its status before retrying."));
    }
    autosave_recovery_state_->candidate_id = candidate_id;
    autosave_recovery_state_->timeline_id = timeline_id;
    autosave_recovery_state_->document_hash = document_hash;
    autosave_recovery_state_->accepted = true;
    autosave_recovery_state_->terminal = false;
    autosave_recovery_state_->succeeded = false;
    autosave_recovery_state_->workspace_activated = false;
  }

  if (!timeline_->openAutosaveRecoveryForAutomation(timeline_id, document_hash)) {
    QMutexLocker lock(&autosave_recovery_state_->mutex);
    autosave_recovery_state_->accepted = false;
    return failure(QStringLiteral("Looksy could not start the exact autosave recovery request."));
  }
  if (!timeline_->recoveryOpenInProgress() || timeline_->recoveryOpenCandidateId() != candidate_id) {
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("accepted"), true},
            {QStringLiteral("recoveryRequired"), true},
            {QStringLiteral("candidateId"), candidate_id},
            {QStringLiteral("error"), QStringLiteral("The autosave recovery request was accepted without a matching pending controller operation; inspect the current Timeline before any retry.")}};
  }
  return {{QStringLiteral("ok"), true},
          {QStringLiteral("accepted"), true},
          {QStringLiteral("terminal"), false},
          {QStringLiteral("status"), QStringLiteral("opening")},
          {QStringLiteral("candidateId"), candidate_id},
          {QStringLiteral("timelineId"), timeline_id},
          {QStringLiteral("expectedDocumentHash"), document_hash},
          {QStringLiteral("replaySafe"), false}};
}

QVariantMap TimelineDraftLifecycle::inspectAutosaveRecovery(const QVariantMap& arguments) const {
  if (arguments.keys() != QStringList{QStringLiteral("documentHash"), QStringLiteral("timelineId")}) {
    return failure(QStringLiteral("Recovery status requires only timelineId and documentHash."));
  }
  const auto timeline_id = arguments.value(QStringLiteral("timelineId")).toString();
  const auto document_hash = arguments.value(QStringLiteral("documentHash")).toString();
  if (!canonical_uuid(timeline_id) || !canonical_sha256(document_hash)) {
    return failure(QStringLiteral("Provide a canonical Timeline ID and lowercase stable document SHA-256."));
  }
  return autosaveRecoveryStatus(timeline_id, document_hash);
}

QVariantMap TimelineDraftLifecycle::autosaveRecoveryStatus(
    const QString& timeline_id,
    const QString& document_hash) const {
  const auto candidate_id = QStringLiteral("autosave:%1:%2").arg(timeline_id, document_hash);
  bool accepted = false;
  bool terminal = false;
  bool succeeded = false;
  bool workspace_activated = false;
  {
    QMutexLocker lock(&autosave_recovery_state_->mutex);
    if (autosave_recovery_state_->candidate_id == candidate_id) {
      accepted = autosave_recovery_state_->accepted;
      terminal = autosave_recovery_state_->terminal;
      succeeded = autosave_recovery_state_->succeeded;
      workspace_activated = autosave_recovery_state_->workspace_activated;
    }
  }

  const auto opening = timeline_->recoveryOpenInProgress() &&
      timeline_->recoveryOpenCandidateId() == candidate_id;
  const auto expected_resource_id = QStringLiteral("timeline:") + timeline_id;
  const auto active_document = timeline_->hasActiveTimeline();
  const auto observed_resource_id = timeline_->agentTaskDocumentId();
  const auto resource_matches = active_document && observed_resource_id == expected_resource_id;
  const auto resource_ready = active_document && timeline_->agentTaskDocumentReady();
  const auto observed_hash = resource_ready && !opening
      ? timeline_->deterministicDocumentHash() : QString{};
  const auto current_document_matches = resource_matches && resource_ready &&
      observed_hash == document_hash;
  if (!terminal && accepted && !opening && current_document_matches) {
    terminal = true;
    succeeded = true;
  }

  QString status;
  if (opening) status = QStringLiteral("opening");
  else if (accepted && terminal && succeeded && resource_matches && !resource_ready)
    status = QStringLiteral("settling");
  else if (terminal && succeeded && current_document_matches && workspace_activated)
    status = QStringLiteral("opened");
  else if (terminal && succeeded && current_document_matches)
    status = QStringLiteral("opened_workspace_activation_unverified");
  else if (!accepted && current_document_matches)
    status = QStringLiteral("matching_document");
  else if (terminal || (accepted && !opening && resource_ready && !current_document_matches))
    status = QStringLiteral("failed");
  else
    status = QStringLiteral("not_requested_or_unknown");

  QVariantMap resource;
  resource.insert(QStringLiteral("resourceId"), observed_resource_id);
  resource.insert(QStringLiteral("ready"), resource_ready);
  resource.insert(QStringLiteral("width"), active_document ? timeline_->sequenceWidth() : 0);
  resource.insert(QStringLiteral("height"), active_document ? timeline_->sequenceHeight() : 0);
  if (!observed_hash.isEmpty()) resource.insert(QStringLiteral("documentHash"), observed_hash);
  return {{QStringLiteral("ok"), true},
          {QStringLiteral("candidateId"), candidate_id},
          {QStringLiteral("timelineId"), timeline_id},
          {QStringLiteral("expectedDocumentHash"), document_hash},
          {QStringLiteral("status"), status},
          {QStringLiteral("accepted"), accepted},
          {QStringLiteral("terminal"), terminal},
          {QStringLiteral("openSucceeded"), terminal && succeeded && current_document_matches},
          {QStringLiteral("documentVerified"), current_document_matches},
          {QStringLiteral("workspaceActivated"), workspace_activated},
          {QStringLiteral("resource"), resource},
          {QStringLiteral("replaySafe"), false}};
}

QVariantMap TimelineDraftLifecycle::activateAfterNavigation(
    QVariantMap result,
    const QString& operation) const {
  if (!activate_timeline_workspace_ || !activate_timeline_workspace_()) {
    result.insert(QStringLiteral("ok"), false);
    result.insert(QStringLiteral("error"), QStringLiteral("The Timeline document is ready, but Main.qml could not activate the native Timeline workspace. Inspect the current document; do not replay navigation."));
    result.insert(QStringLiteral("mutationApplied"), true);
    result.insert(QStringLiteral("operation"), operation);
    result.insert(QStringLiteral("resource"), timeline_->inspectAgentDocument());
    return result;
  }
  return result;
}

}  // namespace photo_editor::desktop

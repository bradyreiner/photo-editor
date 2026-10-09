#include "library_workflow_controller.h"

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

namespace photo_editor::desktop {
namespace {

constexpr int kWorkflowVersion = 1;
constexpr qsizetype kMaximumSearches = 100;
constexpr qsizetype kMaximumFilterBytes = 64 * 1024;

QString normalized_stage(const QString& stage) {
  const auto normalized = stage.trimmed().toLower();
  if (normalized == QStringLiteral("review") ||
      normalized == QStringLiteral("edit") ||
      normalized == QStringLiteral("ready") ||
      normalized == QStringLiteral("delivered") ||
      normalized == QStringLiteral("archived")) {
    return normalized;
  }
  return {};
}

QVariantList searches_from_json(const QJsonValue& value) {
  QVariantList searches;
  if (!value.isArray()) return searches;

  QSet<QString> seen_ids;
  QSet<QString> seen_names;
  const auto array = value.toArray();
  for (const auto& entry : array) {
    if (!entry.isObject()) continue;
    const auto object = entry.toObject();
    const auto id = object.value(QStringLiteral("id")).toString().trimmed();
    const auto name = object.value(QStringLiteral("name")).toString().trimmed();
    const auto filters = object.value(QStringLiteral("filters"));
    const auto normalized_name = name.toCaseFolded();
    if (id.isEmpty() || name.isEmpty() || name.size() > 96 ||
        !filters.isObject() || seen_ids.contains(id) || seen_names.contains(normalized_name)) {
      continue;
    }
    seen_ids.insert(id);
    seen_names.insert(normalized_name);
    QVariantMap search{
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("filters"), filters.toObject().toVariantMap()},
        {QStringLiteral("createdAt"), object.value(QStringLiteral("createdAt")).toString()}};
    searches.push_back(search);
    if (searches.size() >= kMaximumSearches) break;
  }
  return searches;
}

}  // namespace

LibraryWorkflowController::LibraryWorkflowController(
    QString state_file_path,
    QString catalog_scope_key,
    QObject* parent)
    : QObject(parent),
      state_file_path_(QFileInfo(std::move(state_file_path)).absoluteFilePath()),
      catalog_scope_key_(catalog_scope_key.trimmed()) {
  if (catalog_scope_key_.isEmpty()) catalog_scope_key_ = QStringLiteral("default");
  load();
}

bool LibraryWorkflowController::readDocument(QJsonObject* document, QString* error) const {
  if (!document) {
    if (error) *error = QStringLiteral("Workflow state destination is unavailable.");
    return false;
  }

  QFile file(state_file_path_);
  if (!file.exists()) {
    *document = {};
    return true;
  }
  if (!file.open(QIODevice::ReadOnly)) {
    if (error) *error = QStringLiteral("Could not read Library workflow preferences: %1").arg(file.errorString());
    return false;
  }

  QJsonParseError parse_error;
  const auto parsed = QJsonDocument::fromJson(file.readAll(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !parsed.isObject()) {
    if (error) {
      *error = QStringLiteral("Library workflow preferences are malformed; the saved file was left untouched.");
    }
    return false;
  }
  *document = parsed.object();
  return true;
}

void LibraryWorkflowController::load() {
  QJsonObject document;
  QString error;
  if (!readDocument(&document, &error)) {
    setPersistenceError(error);
    return;
  }

  const auto workflow_value = document.value(QStringLiteral("libraryWorkflow"));
  if (workflow_value.isUndefined()) return;
  if (!workflow_value.isObject()) {
    setPersistenceError(QStringLiteral("Library workflow preferences have an unsupported format; the saved file was left untouched."));
    return;
  }
  const auto workflow = workflow_value.toObject();
  const int version = workflow.value(QStringLiteral("version")).toInt(kWorkflowVersion);
  if (version != kWorkflowVersion) {
    setPersistenceError(QStringLiteral("Library workflow preferences use an unsupported version; the saved file was left untouched."));
    return;
  }

  const auto scopes_value = workflow.value(QStringLiteral("scopes"));
  if (scopes_value.isUndefined()) return;
  if (!scopes_value.isObject()) {
    setPersistenceError(QStringLiteral("Library workflow preferences have an unsupported catalog scope map; the saved file was left untouched."));
    return;
  }
  const auto scope_value = scopes_value.toObject().value(catalog_scope_key_);
  if (scope_value.isUndefined()) return;
  if (!scope_value.isObject()) {
    setPersistenceError(QStringLiteral("Library workflow preferences for this catalog are malformed; the saved file was left untouched."));
    return;
  }
  const auto scope = scope_value.toObject();

  if (scope.value(QStringLiteral("navigationCollapsed")).isBool()) {
    navigation_collapsed_ = scope.value(QStringLiteral("navigationCollapsed")).toBool();
  }
  const auto destination = scope.value(QStringLiteral("activeDestination"));
  if (destination.isString() && !destination.toString().trimmed().isEmpty()) {
    active_destination_ = destination.toString().trimmed();
  }

  const auto stages_value = scope.value(QStringLiteral("projectStages"));
  if (stages_value.isObject()) {
    const auto stages = stages_value.toObject();
    for (auto it = stages.constBegin(); it != stages.constEnd(); ++it) {
      const auto stage = normalized_stage(it.value().toString());
      if (!it.key().trimmed().isEmpty() && !stage.isEmpty()) {
        project_stages_.insert(it.key(), stage);
      }
    }
  }
  saved_searches_ = searches_from_json(scope.value(QStringLiteral("savedSearches")));
}

bool LibraryWorkflowController::writeStateField(
    const QString& field,
    const QJsonValue& value,
    QString* error) {
  return writeStateFields(QJsonObject{{field, value}}, error);
}

bool LibraryWorkflowController::writeStateFields(
    const QJsonObject& fields,
    QString* error) {
  if (!QDir().mkpath(QFileInfo(state_file_path_).absolutePath())) {
    if (error) *error = QStringLiteral("Could not create the Library preferences folder.");
    return false;
  }
  QLockFile lock(state_file_path_ + QStringLiteral(".lock"));
  lock.setStaleLockTime(0);
  if (!lock.tryLock(0)) {
    if (error) *error = QStringLiteral("Library preferences are being saved in another window. Try again.");
    return false;
  }
  QJsonObject document;
  if (!readDocument(&document, error)) return false;

  const auto workflow_value = document.value(QStringLiteral("libraryWorkflow"));
  if (!workflow_value.isUndefined() && !workflow_value.isObject()) {
    if (error) *error = QStringLiteral("Library workflow preferences have an unsupported format; the saved file was left untouched.");
    return false;
  }
  auto workflow = workflow_value.toObject();
  const int version = workflow.value(QStringLiteral("version")).toInt(kWorkflowVersion);
  if (version != kWorkflowVersion) {
    if (error) *error = QStringLiteral("Library workflow preferences use an unsupported version; the saved file was left untouched.");
    return false;
  }

  const auto scopes_value = workflow.value(QStringLiteral("scopes"));
  if (!scopes_value.isUndefined() && !scopes_value.isObject()) {
    if (error) *error = QStringLiteral("Library workflow preferences have an unsupported catalog scope map; the saved file was left untouched.");
    return false;
  }
  auto scopes = scopes_value.toObject();
  const auto scope_value = scopes.value(catalog_scope_key_);
  if (!scope_value.isUndefined() && !scope_value.isObject()) {
    if (error) *error = QStringLiteral("Library workflow preferences for this catalog are malformed; the saved file was left untouched.");
    return false;
  }
  auto scope = scope_value.toObject();
  for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
    if (it.key() == QStringLiteral("projectStages")) {
      auto stages = scope.value(it.key()).toObject();
      const auto edits = it.value().toObject();
      for (auto edit = edits.begin(); edit != edits.end(); ++edit) {
        if (project_stages_.value(edit.key()).toString() != edit.value().toString()) stages.insert(edit.key(), edit.value());
      }
      scope.insert(it.key(), stages);
    } else if (it.key() == QStringLiteral("savedSearches")) {
      QHash<QString, QJsonObject> previous, incoming, current;
      for (const auto& search : saved_searches_) {
        const auto object = QJsonObject::fromVariantMap(search.toMap());
        previous.insert(object.value("id").toString(), object);
      }
      for (const auto& search : it.value().toArray()) incoming.insert(search.toObject().value("id").toString(), search.toObject());
      for (const auto& search : scope.value(it.key()).toArray()) current.insert(search.toObject().value("id").toString(), search.toObject());
      for (auto search = previous.begin(); search != previous.end(); ++search) if (!incoming.contains(search.key())) current.remove(search.key());
      for (auto search = incoming.begin(); search != incoming.end(); ++search) if (previous.value(search.key()) != search.value()) current.insert(search.key(), search.value());
      QSet<QString> names;
      QJsonArray searches;
      auto ids = current.keys(); ids.sort();
      for (const auto& id : ids) {
        const auto name = current.value(id).value("name").toString().toCaseFolded();
        if (names.contains(name) || current.size() > kMaximumSearches) {
          if (error) *error = QStringLiteral("Another window saved a conflicting search. Reopen Library preferences and try again.");
          return false;
        }
        names.insert(name); searches.append(current.value(id));
      }
      scope.insert(it.key(), searches);
    } else scope.insert(it.key(), it.value());
  }
  scopes.insert(catalog_scope_key_, scope);
  workflow.insert(QStringLiteral("version"), kWorkflowVersion);
  workflow.insert(QStringLiteral("scopes"), scopes);
  document.insert(QStringLiteral("libraryWorkflow"), workflow);

  const auto parent_path = QFileInfo(state_file_path_).absolutePath();
  if (!QDir().mkpath(parent_path)) {
    if (error) *error = QStringLiteral("Could not create the Library preferences folder.");
    return false;
  }
  QSaveFile file(state_file_path_);
  if (!file.open(QIODevice::WriteOnly)) {
    if (error) *error = QStringLiteral("Could not save Library workflow preferences: %1").arg(file.errorString());
    return false;
  }
  const auto bytes = QJsonDocument(document).toJson(QJsonDocument::Indented);
  if (file.write(bytes) != bytes.size() || !file.commit()) {
    if (error) *error = QStringLiteral("Could not atomically save Library workflow preferences: %1").arg(file.errorString());
    return false;
  }
  if (error) error->clear();
  setPersistenceError({});
  return true;
}

void LibraryWorkflowController::setPersistenceError(const QString& error) {
  if (persistence_error_ == error) return;
  persistence_error_ = error;
  emit persistenceErrorChanged();
}

void LibraryWorkflowController::setNavigationCollapsed(bool collapsed) {
  if (navigation_collapsed_ == collapsed) return;
  QString error;
  if (!writeStateField(QStringLiteral("navigationCollapsed"), collapsed, &error)) {
    setPersistenceError(error);
  }
  navigation_collapsed_ = collapsed;
  emit navigationCollapsedChanged();
}

QString LibraryWorkflowController::stageForProject(const QString& project_name) const {
  const auto name = project_name.trimmed();
  const auto stage = project_stages_.value(name).toString();
  return stage.isEmpty() ? QStringLiteral("review") : stage;
}

bool LibraryWorkflowController::setProjectStage(const QString& project_name, const QString& stage) {
  const auto name = project_name.trimmed();
  const auto normalized = normalized_stage(stage);
  if (name.isEmpty() || name.size() > 256 || normalized.isEmpty()) return false;

  auto next_stages = project_stages_;
  next_stages.insert(name, normalized);
  QString error;
  if (!writeStateField(QStringLiteral("projectStages"), QJsonObject::fromVariantMap(next_stages), &error)) {
    setPersistenceError(error);
    return false;
  }
  project_stages_ = std::move(next_stages);
  emit projectStagesChanged();
  return true;
}

bool LibraryWorkflowController::saveSearch(const QString& name, const QVariantMap& filters) {
  const auto normalized_name = name.trimmed();
  if (normalized_name.isEmpty() || normalized_name.size() > 96 || saved_searches_.size() >= kMaximumSearches) {
    return false;
  }
  if (filters.isEmpty()) return false;
  const auto filters_json = QJsonObject::fromVariantMap(filters);
  if (QJsonDocument(filters_json).toJson(QJsonDocument::Compact).size() > kMaximumFilterBytes) return false;
  const auto folded_name = normalized_name.toCaseFolded();
  for (const auto& value : saved_searches_) {
    const auto search = value.toMap();
    if (search.value(QStringLiteral("name")).toString().toCaseFolded() == folded_name) return false;
  }

  const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const auto created_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
  const QVariantMap search{
      {QStringLiteral("id"), id},
      {QStringLiteral("name"), normalized_name},
      {QStringLiteral("filters"), filters},
      {QStringLiteral("createdAt"), created_at}};
  auto next_searches = saved_searches_;
  next_searches.push_back(search);
  QJsonArray array;
  for (const auto& entry : next_searches) array.push_back(QJsonObject::fromVariantMap(entry.toMap()));
  QString error;
  if (!writeStateField(QStringLiteral("savedSearches"), array, &error)) {
    setPersistenceError(error);
    return false;
  }
  saved_searches_ = std::move(next_searches);
  emit savedSearchesChanged();
  return true;
}

bool LibraryWorkflowController::deleteSearch(const QString& search_id) {
  const auto id = search_id.trimmed();
  if (id.isEmpty()) return false;
  auto next_searches = saved_searches_;
  const auto old_size = next_searches.size();
  for (qsizetype index = next_searches.size() - 1; index >= 0; --index) {
    if (next_searches.at(index).toMap().value(QStringLiteral("id")).toString() == id) {
      next_searches.removeAt(index);
    }
  }
  if (next_searches.size() == old_size) return false;

  QJsonArray array;
  for (const auto& entry : next_searches) array.push_back(QJsonObject::fromVariantMap(entry.toMap()));
  const bool deleting_active_search =
      active_destination_ == QStringLiteral("saved-search:") + id;
  QJsonObject changed_fields{{QStringLiteral("savedSearches"), array}};
  if (deleting_active_search) {
    changed_fields.insert(QStringLiteral("activeDestination"), QStringLiteral("all-images"));
  }
  QString error;
  if (!writeStateFields(changed_fields, &error)) {
    setPersistenceError(error);
    return false;
  }
  saved_searches_ = std::move(next_searches);
  emit savedSearchesChanged();
  if (deleting_active_search) {
    active_destination_ = QStringLiteral("all-images");
    emit activeDestinationChanged();
    emit allImagesRequested();
  }
  return true;
}

bool LibraryWorkflowController::activateDestination(const QString& destination) {
  const auto next_destination = destination.trimmed();
  if (next_destination.isEmpty()) return false;
  if (active_destination_ != next_destination) {
    QString error;
    if (!writeStateField(QStringLiteral("activeDestination"), next_destination, &error)) {
      setPersistenceError(error);
    }
    active_destination_ = next_destination;
    emit activeDestinationChanged();
  }
  return true;
}

bool LibraryWorkflowController::activateSavedSearch(const QString& search_id) {
  const auto id = search_id.trimmed();
  for (const auto& value : saved_searches_) {
    const auto search = value.toMap();
    if (search.value(QStringLiteral("id")).toString() != id) continue;
    activateDestination(QStringLiteral("saved-search:") + id);
    emit savedSearchRequested(id, search.value(QStringLiteral("filters")).toMap());
    return true;
  }
  return false;
}

void LibraryWorkflowController::requestAllImages() {
  activateDestination(QStringLiteral("all-images"));
  emit allImagesRequested();
}

void LibraryWorkflowController::requestRecentImports() {
  activateDestination(QStringLiteral("recent-imports"));
  emit recentImportsRequested();
}

void LibraryWorkflowController::requestProject(const QString& project_name) {
  const auto name = project_name.trimmed();
  if (name.isEmpty()) return;
  activateDestination(QStringLiteral("project:") + name);
  emit projectRequested(name);
}

void LibraryWorkflowController::requestImportSession(const QString& job_id) {
  const auto id = job_id.trimmed();
  if (id.isEmpty()) return;
  activateDestination(QStringLiteral("import:") + id);
  emit importSessionRequested(id);
}

}  // namespace photo_editor::desktop

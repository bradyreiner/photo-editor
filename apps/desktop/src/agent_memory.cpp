#include "agent_memory.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>
#include <QVariantList>

namespace photo_editor::desktop {
namespace {

constexpr int kAgentMemorySchemaVersion = 1;

struct RelatedMemoryNote {
  QString section;
  QString type;
  QString note_id;
  QString title;
  QString link;
};

QString safe_memory_id(const QString& value, const QString& fallback) {
  auto normalized = value.trimmed().toLower();
  normalized.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
  normalized.replace(QRegularExpression(QStringLiteral("^-+|-+$")), QString());
  normalized = normalized.left(72);
  normalized.replace(QRegularExpression(QStringLiteral("-+$")), QString());
  return normalized.isEmpty() ? fallback : normalized;
}

QVariantMap map_value(
    const QVariantMap& values,
    const QString& runtime_key,
    const QString& storage_key) {
  const auto runtime_value = values.value(runtime_key);
  if (runtime_value.isValid() && !runtime_value.toMap().isEmpty()) {
    return runtime_value.toMap();
  }
  return values.value(storage_key).toMap();
}

QStringList string_list_value(const QVariant& value) {
  const auto direct = value.toStringList();
  if (!direct.isEmpty()) {
    return direct;
  }
  QStringList output;
  for (const auto& item : value.toList()) {
    const auto text = item.toString().trimmed();
    if (!text.isEmpty()) {
      output.append(text);
    }
  }
  return output;
}

QVariantList limited_variant_list(const QVariant& value, int maximum_count) {
  const auto values = value.toList();
  QVariantList limited;
  const auto count = std::min(static_cast<int>(values.size()), std::max(0, maximum_count));
  limited.reserve(count);
  for (auto index = 0; index < count; ++index) {
    limited.append(values.at(index));
  }
  return limited;
}

QString markdown_escape(QString value) {
  value.replace(QLatin1Char('\r'), QLatin1Char(' '));
  return value.trimmed();
}

QString string_or_fallback(const QVariantMap& values, const QString& key, const QString& fallback) {
  const auto value = values.value(key).toString().trimmed();
  return value.isEmpty() ? fallback : value;
}

QString front_matter_string(const QString& value) {
  auto escaped = value;
  escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
  escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
  return QStringLiteral("\"%1\"").arg(escaped);
}

QString schema_version_front_matter() {
  return QStringLiteral("schema_version: %1\n").arg(kAgentMemorySchemaVersion);
}

bool write_text_file(const QString& path, const QString& text, QString* error_message) {
  const QFileInfo info(path);
  if (!QDir().mkpath(info.dir().absolutePath())) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Unable to create agent memory directory: %1")
                           .arg(info.dir().absolutePath());
    }
    return false;
  }

  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Unable to write agent memory note: %1").arg(path);
    }
    return false;
  }
  const auto bytes = text.toUtf8();
  if (file.write(bytes) != bytes.size()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Unable to write complete agent memory note: %1").arg(path);
    }
    return false;
  }
  if (!file.commit()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Unable to commit agent memory note: %1").arg(path);
    }
    return false;
  }
  return true;
}

QString json_block(const QVariant& value) {
  return QString::fromUtf8(QJsonDocument::fromVariant(value).toJson(QJsonDocument::Indented)).trimmed();
}

int markdown_note_count(const QDir& root, const QString& directory_name) {
  const QDir directory(root.filePath(directory_name));
  if (!directory.exists()) {
    return 0;
  }
  return static_cast<int>(
      directory.entryList(QStringList{QStringLiteral("*.md")}, QDir::Files).size());
}

QString memory_timestamp_id(const QString& fallback) {
  return safe_memory_id(
      QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz")),
      fallback);
}

QStringList compact_text_list(const QVariant& value, int limit = 12) {
  auto values = string_list_value(value);
  if (!values.isEmpty()) {
    return values.mid(0, limit);
  }
  for (const auto& item : value.toList()) {
    const auto text = item.toString().trimmed();
    if (!text.isEmpty()) {
      values.append(text);
    }
    if (values.size() >= limit) {
      break;
    }
  }
  return values;
}

QString markdown_list_or_none(const QStringList& values) {
  if (values.isEmpty()) {
    return QStringLiteral("- None recorded\n");
  }
  QString text;
  for (const auto& value : values) {
    text += QStringLiteral("- `%1`\n").arg(markdown_escape(value));
  }
  return text;
}

QString safe_app_data_artifact_link(
    const QString& app_data_location,
    const QString& artifact_path) {
  const auto trimmed_path = artifact_path.trimmed();
  if (trimmed_path.isEmpty()) {
    return QString();
  }
  const QFileInfo info(trimmed_path);
  if (!info.isAbsolute()) {
    return QString();
  }
  const QDir app_data_dir(app_data_location);
  const auto relative_path = QDir::cleanPath(app_data_dir.relativeFilePath(info.absoluteFilePath()));
  if (relative_path == QStringLiteral("..") ||
      relative_path.startsWith(QStringLiteral("../")) ||
      !relative_path.startsWith(QStringLiteral("agent-runs/"))) {
    return QString();
  }
  return relative_path;
}

void append_safe_artifact_reference(
    QVariantList* references,
    const QString& app_data_location,
    const QString& label,
    const QString& artifact_path,
    const QString& asset_id = QString()) {
  if (references == nullptr) {
    return;
  }
  const auto relative_path = safe_app_data_artifact_link(app_data_location, artifact_path);
  if (relative_path.isEmpty()) {
    return;
  }
  for (const auto& value : *references) {
    const auto reference = value.toMap();
    if (reference.value(QStringLiteral("relativePath")).toString() == relative_path) {
      return;
    }
  }
  QVariantMap reference{
      {QStringLiteral("label"), label},
      {QStringLiteral("relativePath"), relative_path},
      {QStringLiteral("markdownHref"), QStringLiteral("../%1").arg(relative_path)},
      {QStringLiteral("storagePolicy"), QStringLiteral("app-owned-generated-artifact")},
  };
  if (!asset_id.trimmed().isEmpty()) {
    reference.insert(QStringLiteral("assetId"), asset_id.trimmed());
  }
  references->append(reference);
}

QVariantList safe_artifact_references_for_run(
    const QString& app_data_location,
    const QVariantMap& run_report) {
  QVariantList references;
  append_safe_artifact_reference(
      &references,
      app_data_location,
      QStringLiteral("Before proof"),
      run_report.value(QStringLiteral("beforeProofPath")).toString());
  append_safe_artifact_reference(
      &references,
      app_data_location,
      QStringLiteral("After proof"),
      run_report.value(QStringLiteral("afterProofPath")).toString());
  append_safe_artifact_reference(
      &references,
      app_data_location,
      QStringLiteral("Validation report"),
      run_report.value(QStringLiteral("validationReportPath")).toString());
  append_safe_artifact_reference(
      &references,
      app_data_location,
      QStringLiteral("Recipe diff"),
      run_report.value(QStringLiteral("recipeDiffPath")).toString());
  append_safe_artifact_reference(
      &references,
      app_data_location,
      QStringLiteral("Run manifest"),
      run_report.value(QStringLiteral("manifestPath")).toString());
  append_safe_artifact_reference(
      &references,
      app_data_location,
      QStringLiteral("Redacted manifest"),
      run_report.value(QStringLiteral("shareableManifestPath")).toString());

  const auto artifacts = run_report.value(QStringLiteral("artifacts")).toMap();
  append_safe_artifact_reference(
      &references,
      app_data_location,
      QStringLiteral("Before proof"),
      artifacts.value(QStringLiteral("beforeProofPath")).toString());
  append_safe_artifact_reference(
      &references,
      app_data_location,
      QStringLiteral("After proof"),
      artifacts.value(QStringLiteral("afterProofPath")).toString());
  append_safe_artifact_reference(
      &references,
      app_data_location,
      QStringLiteral("Validation report"),
      artifacts.value(QStringLiteral("validationReportPath")).toString());
  append_safe_artifact_reference(
      &references,
      app_data_location,
      QStringLiteral("Recipe diff"),
      artifacts.value(QStringLiteral("recipeDiffPath")).toString());

  auto asset_reference_count = 0;
  for (const auto& asset_report_value : run_report.value(QStringLiteral("assetReports")).toList()) {
    const auto asset_report = asset_report_value.toMap();
    const auto asset_id = asset_report.value(QStringLiteral("assetId")).toString();
    const auto validation_report = asset_report.value(QStringLiteral("validationReport")).toMap();
    const auto asset_artifacts = validation_report.value(QStringLiteral("artifacts")).toMap();
    append_safe_artifact_reference(
        &references,
        app_data_location,
        QStringLiteral("Asset after proof"),
        asset_artifacts.value(QStringLiteral("afterProofPath")).toString(),
        asset_id);
    ++asset_reference_count;
    if (asset_reference_count >= 8 || references.size() >= 18) {
      break;
    }
  }
  return references;
}

QString linked_style_text(const QString& style_id) {
  return style_id.trimmed().isEmpty()
             ? QStringLiteral("None")
             : QStringLiteral("[[%1]]").arg(safe_memory_id(style_id, QStringLiteral("style")));
}

QString linked_note_text_from_id(const QString& note_id) {
  return note_id.trimmed().isEmpty()
             ? QString()
             : QStringLiteral("[[%1]]").arg(safe_memory_id(note_id, QStringLiteral("note")));
}

QStringList unique_links(QStringList links) {
  links.removeAll(QString());
  links.removeDuplicates();
  std::sort(links.begin(), links.end());
  return links;
}

QString front_matter_link_list(const QString& key, const QStringList& links) {
  const auto normalized_links = unique_links(links);
  if (normalized_links.isEmpty()) {
    return QStringLiteral("%1: []\n").arg(key);
  }
  QString text = QStringLiteral("%1:\n").arg(key);
  for (const auto& link : normalized_links) {
    text += QStringLiteral("  - \"%1\"\n").arg(link);
  }
  return text;
}

QString front_matter_field(const QString& text, const QString& key) {
  const QRegularExpression expression(
      QStringLiteral("^%1:\\s*\"?([^\"\\n]*)\"?\\s*$").arg(QRegularExpression::escape(key)),
      QRegularExpression::MultilineOption);
  const auto match = expression.match(text);
  return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

QString first_markdown_heading(const QString& text, const QString& fallback) {
  const QRegularExpression expression(
      QStringLiteral("^#\\s+(.+)\\s*$"),
      QRegularExpression::MultilineOption);
  const auto match = expression.match(text);
  return match.hasMatch() ? match.captured(1).trimmed() : fallback;
}

QString related_memory_label(const QString& type, const QString& section) {
  if (type == QStringLiteral("agent_project_memory")) {
    return QStringLiteral("Project");
  }
  if (type == QStringLiteral("agent_feedback_memory")) {
    return QStringLiteral("Feedback");
  }
  if (type == QStringLiteral("agent_run_memory")) {
    return QStringLiteral("Run");
  }
  if (type == QStringLiteral("agent_issue_memory")) {
    return QStringLiteral("Issue");
  }
  if (type == QStringLiteral("agent_tool_memory")) {
    return QStringLiteral("Tool rulebook");
  }
  return section;
}

QVariantMap agent_memory_schema_status(const QString& root_path) {
  QVariantList mismatches;
  auto checked_count = 0;
  if (QDir(root_path).exists()) {
    QDirIterator iterator(
        root_path,
        QStringList{QStringLiteral("*.md")},
        QDir::Files,
        QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
      const auto path = iterator.next();
      QFile file(path);
      if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        mismatches.append(QVariantMap{
            {QStringLiteral("path"), path},
            {QStringLiteral("reason"), QStringLiteral("unreadable-note")},
        });
        continue;
      }
      ++checked_count;
      const auto text = QString::fromUtf8(file.readAll());
      const auto version_text = front_matter_field(text, QStringLiteral("schema_version"));
      bool parsed = false;
      const auto version = version_text.toInt(&parsed);
      if (!parsed || version != kAgentMemorySchemaVersion) {
        mismatches.append(QVariantMap{
            {QStringLiteral("path"), path},
            {QStringLiteral("foundSchemaVersion"), parsed ? QVariant(version) : QVariant(version_text)},
            {QStringLiteral("expectedSchemaVersion"), kAgentMemorySchemaVersion},
            {QStringLiteral("reason"), version_text.trimmed().isEmpty()
                                          ? QStringLiteral("missing-schema-version")
                                          : QStringLiteral("schema-version-mismatch")},
        });
      }
    }
  }
  return QVariantMap{
      {QStringLiteral("schemaVersion"), kAgentMemorySchemaVersion},
      {QStringLiteral("schemaVersionOk"), mismatches.isEmpty()},
      {QStringLiteral("schemaCheckedNoteCount"), checked_count},
      {QStringLiteral("schemaMismatchCount"), mismatches.size()},
      {QStringLiteral("schemaMismatches"), mismatches},
      {QStringLiteral("migrationRequired"), !mismatches.isEmpty()},
      {QStringLiteral("migrationStrategy"), QStringLiteral("manual-or-future-app-migrator")},
  };
}

QList<RelatedMemoryNote> related_memory_notes_for_style(
    const QString& app_data_location,
    const QString& style_id) {
  QList<RelatedMemoryNote> notes;
  if (style_id.trimmed().isEmpty()) {
    return notes;
  }
  const auto root_path = agent_memory_root_directory(app_data_location);
  const QStringList sections{
      QStringLiteral("tools"),
      QStringLiteral("projects"),
      QStringLiteral("feedback"),
      QStringLiteral("runs"),
      QStringLiteral("issues"),
  };
  for (const auto& section : sections) {
    QDir directory(QDir(root_path).filePath(section));
    if (!directory.exists()) {
      continue;
    }
    const auto files = directory.entryList(QStringList{QStringLiteral("*.md")}, QDir::Files, QDir::Name);
    for (const auto& file_name : files) {
      const auto path = directory.filePath(file_name);
      QFile file(path);
      if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        continue;
      }
      const auto text = QString::fromUtf8(file.readAll());
      if (front_matter_field(text, QStringLiteral("style_id")) != style_id) {
        continue;
      }
      const auto note_id = QFileInfo(path).completeBaseName();
      notes.append(RelatedMemoryNote{
          section,
          front_matter_field(text, QStringLiteral("type")),
          note_id,
          first_markdown_heading(text, note_id),
          linked_note_text_from_id(note_id),
      });
    }
  }
  return notes;
}

QString issue_memory_text(QString value) {
  value = markdown_escape(value);
  value.replace(QRegularExpression(QStringLiteral("data:image[^\\s`]+")), QStringLiteral("<image-data>"));
  value.replace(QRegularExpression(QStringLiteral("base64[^\\s`]*")), QStringLiteral("<encoded-data>"));
  value.replace(QRegularExpression(QStringLiteral("(/[^\\s`]+)+")), QStringLiteral("<local-path>"));
  return value;
}

QString redacted_memory_text(QString value, const QString& app_data_location) {
  value.replace(app_data_location, QStringLiteral("<app-data>"));
  value.replace(QRegularExpression(QStringLiteral("data:image[^\\s`]+")), QStringLiteral("<image-data>"));
  value.replace(QRegularExpression(QStringLiteral("base64[^\\s`]*")), QStringLiteral("<encoded-data>"));
  value.replace(QRegularExpression(QStringLiteral("(/[^\\s`]+)+")), QStringLiteral("<local-path>"));
  return value;
}

int recursive_file_count(const QString& root_path) {
  if (!QDir(root_path).exists()) {
    return 0;
  }
  auto count = 0;
  QDirIterator iterator(root_path, QDir::Files, QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    iterator.next();
    ++count;
  }
  return count;
}

void append_unique_text(QStringList* values, const QVariant& value, int limit = 24) {
  if (values == nullptr || values->size() >= limit) {
    return;
  }
  const auto text = value.toString().trimmed();
  if (!text.isEmpty() && !values->contains(text)) {
    values->append(text);
  }
}

QStringList warnings_from_report(const QVariantMap& report, int limit = 24) {
  QStringList warnings;
  for (const auto& warning : string_list_value(report.value(QStringLiteral("warnings")))) {
    append_unique_text(&warnings, warning, limit);
  }
  const auto validation_report = report.value(QStringLiteral("validationReport")).toMap();
  for (const auto& warning : string_list_value(validation_report.value(QStringLiteral("warnings")))) {
    append_unique_text(&warnings, warning, limit);
  }
  for (const auto& asset_report_value : report.value(QStringLiteral("assetReports")).toList()) {
    const auto asset_report = asset_report_value.toMap();
    for (const auto& warning : string_list_value(asset_report.value(QStringLiteral("warnings")))) {
      append_unique_text(&warnings, warning, limit);
    }
    const auto asset_validation = asset_report.value(QStringLiteral("validationReport")).toMap();
    for (const auto& warning : string_list_value(asset_validation.value(QStringLiteral("warnings")))) {
      append_unique_text(&warnings, warning, limit);
    }
  }
  return warnings;
}

QStringList errors_from_report(const QVariantMap& report, int limit = 16) {
  QStringList errors;
  for (const auto& error : string_list_value(report.value(QStringLiteral("errors")))) {
    append_unique_text(&errors, error, limit);
  }
  const auto validation_report = report.value(QStringLiteral("validationReport")).toMap();
  for (const auto& error : string_list_value(validation_report.value(QStringLiteral("errors")))) {
    append_unique_text(&errors, error, limit);
  }
  for (const auto& asset_report_value : report.value(QStringLiteral("assetReports")).toList()) {
    const auto asset_report = asset_report_value.toMap();
    for (const auto& error : string_list_value(asset_report.value(QStringLiteral("errors")))) {
      append_unique_text(&errors, error, limit);
    }
    const auto asset_validation = asset_report.value(QStringLiteral("validationReport")).toMap();
    for (const auto& error : string_list_value(asset_validation.value(QStringLiteral("errors")))) {
      append_unique_text(&errors, error, limit);
    }
  }
  return errors;
}

QStringList outlier_asset_ids_from_report(const QVariantMap& report) {
  QStringList asset_ids;
  for (const auto& cluster_value : report.value(QStringLiteral("batchClusters")).toList()) {
    for (const auto& asset_id : cluster_value.toMap().value(QStringLiteral("outlierAssetIds")).toStringList()) {
      append_unique_text(&asset_ids, asset_id, 24);
    }
  }
  for (const auto& asset_report_value : report.value(QStringLiteral("assetReports")).toList()) {
    const auto asset_report = asset_report_value.toMap();
    const auto style_match =
        asset_report.value(QStringLiteral("validationReport")).toMap().value(QStringLiteral("styleMatch")).toMap();
    if (style_match.value(QStringLiteral("needsReview")).toBool()) {
      append_unique_text(&asset_ids, asset_report.value(QStringLiteral("assetId")), 24);
    }
  }
  return asset_ids;
}

int model_repair_count_from_report(const QVariantMap& report) {
  auto count = report.value(QStringLiteral("modelRepairCount")).toInt();
  for (const auto& asset_report_value : report.value(QStringLiteral("assetReports")).toList()) {
    count += asset_report_value.toMap().value(QStringLiteral("modelRepairCount")).toInt();
  }
  return count;
}

QVariantMap issue_summary_for_run(const QVariantMap& run_report) {
  const auto warnings = warnings_from_report(run_report);
  const auto errors = errors_from_report(run_report);
  const auto outlier_asset_ids = outlier_asset_ids_from_report(run_report);
  const auto model_repair_count = model_repair_count_from_report(run_report);
  const auto validation_report = run_report.value(QStringLiteral("validationReport")).toMap();
  const auto has_validation_failure =
      validation_report.contains(QStringLiteral("ok")) &&
      !validation_report.value(QStringLiteral("ok")).toBool();
  const auto has_issues =
      !warnings.isEmpty() ||
      !errors.isEmpty() ||
      !outlier_asset_ids.isEmpty() ||
      model_repair_count > 0 ||
      has_validation_failure;
  return QVariantMap{
      {QStringLiteral("hasIssues"), has_issues},
      {QStringLiteral("warningCount"), warnings.size()},
      {QStringLiteral("errorCount"), errors.size()},
      {QStringLiteral("warnings"), warnings},
      {QStringLiteral("errors"), errors},
      {QStringLiteral("outlierAssetIds"), outlier_asset_ids},
      {QStringLiteral("outlierCount"), outlier_asset_ids.size()},
      {QStringLiteral("modelRepairCount"), model_repair_count},
      {QStringLiteral("validationOk"),
       validation_report.contains(QStringLiteral("ok"))
           ? validation_report.value(QStringLiteral("ok"))
           : QVariant()},
  };
}

QVariantMap allowed_tools_map(const QVariantMap& style) {
  return map_value(style, QStringLiteral("allowedTools"), QStringLiteral("allowed_tools"));
}

QVariantMap tool_knowledge_map(const QVariantMap& style) {
  return map_value(style, QStringLiteral("toolKnowledge"), QStringLiteral("tool_knowledge"));
}

QString tool_policy_label(const QVariantMap& allowed_tools, const QString& tool_key) {
  const auto value = allowed_tools.value(tool_key);
  if (!value.isValid()) {
    return QStringLiteral("not specified");
  }
  if (value.typeId() == QMetaType::Bool) {
    return value.toBool() ? QStringLiteral("enabled") : QStringLiteral("disabled");
  }
  const auto text = value.toString().trimmed();
  return text.isEmpty() ? QStringLiteral("configured") : text;
}

QVariantList compact_tool_cards_for_style(const QVariantMap& style, int limit = 12) {
  const auto allowed_tools = allowed_tools_map(style);
  const auto tool_knowledge = tool_knowledge_map(style);
  QVariantList cards;
  for (auto iterator = tool_knowledge.cbegin(); iterator != tool_knowledge.cend(); ++iterator) {
    if (cards.size() >= limit) {
      break;
    }
    const auto key = iterator.key();
    cards.append(QVariantMap{
        {QStringLiteral("name"), key},
        {QStringLiteral("policy"), tool_policy_label(allowed_tools, key)},
        {QStringLiteral("rule"), iterator.value().toString()},
    });
  }
  return cards;
}

QVariantList retrieval_plan_for_phase(const QString& phase) {
  if (phase == QStringLiteral("setup")) {
    return {};
  }
  if (phase == QStringLiteral("revision")) {
    return {
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("run")},
                    {QStringLiteral("maxCards"), 1},
                    {QStringLiteral("reason"), QStringLiteral("Current sample result and previous model decision.")}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("feedback")},
                    {QStringLiteral("maxCards"), 1},
                    {QStringLiteral("reason"), QStringLiteral("Latest user correction for the active style.")}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("issue")},
                    {QStringLiteral("maxCards"), 1},
                    {QStringLiteral("reason"), QStringLiteral("Failed validation checks and repair strategy.")}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("tool")},
                    {QStringLiteral("maxCards"), 4},
                    {QStringLiteral("reason"), QStringLiteral("Only tools relevant to repair and validation.")}},
    };
  }
  if (phase == QStringLiteral("batch")) {
    return {
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("approved-sample-recipe")},
                    {QStringLiteral("maxCards"), 1},
                    {QStringLiteral("reason"), QStringLiteral("Use the approved or latest validated sample as the batch anchor.")}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("cluster-summary")},
                    {QStringLiteral("maxCards"), 6},
                    {QStringLiteral("reason"), QStringLiteral("Summarize lighting/camera clusters without listing every photo.")}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("style-constraints")},
                    {QStringLiteral("maxCards"), 1},
                    {QStringLiteral("reason"), QStringLiteral("Keep batch edits consistent with active style limits.")}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("tool")},
                    {QStringLiteral("maxCards"), 4},
                    {QStringLiteral("reason"), QStringLiteral("Validation and bulk-planning tools only.")}},
    };
  }
  if (phase == QStringLiteral("outlier")) {
    return {
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("issue")},
                    {QStringLiteral("maxCards"), 1},
                    {QStringLiteral("reason"), QStringLiteral("Only the current outlier issue and validation report.")}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("run")},
                    {QStringLiteral("maxCards"), 1},
                    {QStringLiteral("reason"), QStringLiteral("Prior fix from the active run, not old unrelated runs.")}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("tool")},
                    {QStringLiteral("maxCards"), 3},
                    {QStringLiteral("reason"), QStringLiteral("Repair and rollback tools for one photo.")}},
    };
  }
  return {
      QVariantMap{{QStringLiteral("kind"), QStringLiteral("style")},
                  {QStringLiteral("maxCards"), 1},
                  {QStringLiteral("reason"), QStringLiteral("Active style card and learned profile summary.")}},
      QVariantMap{{QStringLiteral("kind"), QStringLiteral("project")},
                  {QStringLiteral("maxCards"), 1},
                  {QStringLiteral("reason"), QStringLiteral("Current project brief and representative examples.")}},
      QVariantMap{{QStringLiteral("kind"), QStringLiteral("feedback")},
                  {QStringLiteral("maxCards"), 1},
                  {QStringLiteral("reason"), QStringLiteral("Latest approved user correction only.")}},
      QVariantMap{{QStringLiteral("kind"), QStringLiteral("tool")},
                  {QStringLiteral("maxCards"), 4},
                  {QStringLiteral("reason"), QStringLiteral("Tool knowledge needed for the sample phase.")}},
  };
}

QStringList included_memory_kinds_from_plan(const QVariantList& retrieval_plan) {
  QStringList kinds;
  for (const auto& value : retrieval_plan) {
    const auto kind = value.toMap().value(QStringLiteral("kind")).toString();
    if (!kind.isEmpty() && !kinds.contains(kind)) {
      kinds.append(kind);
    }
  }
  return kinds;
}

QVariantMap memory_budget_for_phase(const QString& phase, const QVariantList& retrieval_plan) {
  auto max_cards = 0;
  for (const auto& value : retrieval_plan) {
    max_cards += value.toMap().value(QStringLiteral("maxCards")).toInt();
  }
  auto token_budget = 1400;
  if (phase == QStringLiteral("setup")) {
    token_budget = 0;
  } else if (phase == QStringLiteral("batch")) {
    token_budget = 2200;
  } else if (phase == QStringLiteral("revision") || phase == QStringLiteral("outlier")) {
    token_budget = 1600;
  }
  return {
      {QStringLiteral("maxCards"), max_cards},
      {QStringLiteral("approxTokenBudget"), token_budget},
      {QStringLiteral("rawVaultIncluded"), false},
      {QStringLiteral("rawArtifactsIncluded"), false},
      {QStringLiteral("imageBytesIncluded"), false},
  };
}

QVariantMap memory_relevance_policy(const QString& phase) {
  return {
      {QStringLiteral("phaseScoped"), true},
      {QStringLiteral("activeStyleOnly"), phase != QStringLiteral("setup")},
      {QStringLiteral("deterministicFirstPass"), true},
      {QStringLiteral("maxRunCards"), 1},
      {QStringLiteral("maxIssueCards"), phase == QStringLiteral("revision") || phase == QStringLiteral("outlier") ? 1 : 0},
      {QStringLiteral("oldMemorySuppression"), QStringLiteral("active-style-active-project-latest-run-first")},
      {QStringLiteral("secondarySimilaritySearch"),
       QVariantMap{
           {QStringLiteral("enabled"), false},
           {QStringLiteral("role"), QStringLiteral("secondary-ranking-only")},
           {QStringLiteral("fallback"), QStringLiteral("deterministic-recency-and-phase-rules")},
       }},
  };
}

QString style_note_markdown(const QString& app_data_location, const QVariantMap& style) {
  const auto style_id = style.value(QStringLiteral("id")).toString();
  const auto style_name = string_or_fallback(style, QStringLiteral("name"), style_id);
  const auto goal = style.value(QStringLiteral("goal")).toString();
  const auto learned_profile =
      map_value(style, QStringLiteral("learnedProfile"), QStringLiteral("learned_profile"));
  const auto tool_knowledge =
      map_value(style, QStringLiteral("toolKnowledge"), QStringLiteral("tool_knowledge"));
  const auto quality_model =
      map_value(style, QStringLiteral("qualityModel"), QStringLiteral("quality_model"));
  const auto project_workflow =
      map_value(style, QStringLiteral("projectWorkflow"), QStringLiteral("project_workflow"));
  const auto references = learned_profile.value(QStringLiteral("reference_examples")).toList();
  const auto guidelines =
      string_list_value(learned_profile.value(QStringLiteral("consistencyGuidelines")));
  const auto feedback_events =
      learned_profile.value(QStringLiteral("feedback_events")).toList();
  const auto related_notes = related_memory_notes_for_style(app_data_location, style_id);
  QStringList related_links;
  for (const auto& note : related_notes) {
    related_links.append(note.link);
  }

  QString text;
  text += QStringLiteral("---\n");
  text += QStringLiteral("type: agent_style_memory\n");
  text += schema_version_front_matter();
  text += QStringLiteral("style_id: %1\n").arg(front_matter_string(style_id));
  text += QStringLiteral("style_name: %1\n").arg(front_matter_string(style_name));
  text += QStringLiteral("updated_at_utc: %1\n")
              .arg(front_matter_string(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)));
  text += QStringLiteral("tags:\n");
  text += QStringLiteral("  - agent-memory\n");
  text += QStringLiteral("  - style\n");
  text += QStringLiteral("  - style/%1\n").arg(safe_memory_id(style_name, QStringLiteral("unnamed")));
  text += front_matter_link_list(QStringLiteral("links"), related_links);
  text += front_matter_link_list(QStringLiteral("backlinks"), related_links);
  text += QStringLiteral("---\n\n");
  text += QStringLiteral("# %1\n\n").arg(markdown_escape(style_name));
  if (!goal.trimmed().isEmpty()) {
    text += QStringLiteral("Goal: %1\n\n").arg(markdown_escape(goal));
  }
  text += QStringLiteral("## Learned Profile\n\n");
  text += QStringLiteral("- Source assets: %1\n")
              .arg(learned_profile.value(QStringLiteral("source_asset_count")).toInt());
  text += QStringLiteral("- Usable recipes: %1\n")
              .arg(learned_profile.value(QStringLiteral("usable_recipe_count")).toInt());
  text += QStringLiteral("- Skipped recipes: %1\n")
              .arg(learned_profile.value(QStringLiteral("skipped_recipe_count")).toInt());
  text += QStringLiteral("- Before/after pairs: %1\n")
              .arg(learned_profile.value(QStringLiteral("before_after_pair_count")).toInt());
  const auto style_summary = learned_profile.value(QStringLiteral("styleSummary")).toString();
  if (!style_summary.trimmed().isEmpty()) {
    text += QStringLiteral("\n%1\n").arg(markdown_escape(style_summary));
  }

  if (!guidelines.isEmpty()) {
    text += QStringLiteral("\n## Consistency Guidelines\n\n");
    for (const auto& guideline : guidelines) {
      text += QStringLiteral("- %1\n").arg(markdown_escape(guideline));
    }
  }

  if (!references.isEmpty()) {
    text += QStringLiteral("\n## Reference Examples\n\n");
    auto index = 0;
    for (const auto& reference_value : references) {
      const auto reference = reference_value.toMap();
      const auto asset_id = reference.value(QStringLiteral("assetId")).toString();
      const auto camera = reference.value(QStringLiteral("cameraLabel")).toString();
      const auto lens = reference.value(QStringLiteral("lensModel")).toString();
      text += QStringLiteral("- `%1`").arg(markdown_escape(asset_id));
      QStringList details;
      if (!camera.trimmed().isEmpty()) {
        details.append(camera.trimmed());
      }
      if (!lens.trimmed().isEmpty()) {
        details.append(lens.trimmed());
      }
      if (!details.isEmpty()) {
        text += QStringLiteral(" - %1").arg(markdown_escape(details.join(QStringLiteral(", "))));
      }
      text += QLatin1Char('\n');
      ++index;
      if (index >= 24) {
        break;
      }
    }
  }

  if (!feedback_events.isEmpty()) {
    text += QStringLiteral("\n## Feedback History\n\n");
    for (const auto& event_value : feedback_events) {
      const auto event = event_value.toMap();
      text += QStringLiteral("- %1: %2\n")
                  .arg(
                      markdown_escape(event.value(QStringLiteral("createdAtUtc")).toString()),
                      markdown_escape(event.value(QStringLiteral("feedback")).toString()));
    }
  }

  text += QStringLiteral("\n## Related Memory\n\n");
  if (related_notes.isEmpty()) {
    text += QStringLiteral("- No related project, feedback, run, issue, or tool notes yet.\n");
  } else {
    for (const auto& note : related_notes) {
      text += QStringLiteral("- %1: %2 - %3\n")
                  .arg(
                      markdown_escape(related_memory_label(note.type, note.section)),
                      note.link,
                      markdown_escape(note.title));
    }
  }

  text += QStringLiteral("\n## Edit Tendencies\n\n```json\n%1\n```\n")
              .arg(json_block(learned_profile.value(QStringLiteral("editTendencies"))));
  if (!tool_knowledge.isEmpty()) {
    text += QStringLiteral("\n## Tool Knowledge\n\n```json\n%1\n```\n").arg(json_block(tool_knowledge));
  }
  if (!quality_model.isEmpty()) {
    text += QStringLiteral("\n## Quality Model\n\n```json\n%1\n```\n").arg(json_block(quality_model));
  }
  if (!project_workflow.isEmpty()) {
    text += QStringLiteral("\n## Project Workflow\n\n```json\n%1\n```\n")
                .arg(json_block(project_workflow));
  }
  return text;
}

QString feedback_note_markdown(
    const QVariantMap& style,
    const QString& feedback,
    const QVariantMap& context,
    const QVariantMap& last_run_report) {
  const auto style_id = style.value(QStringLiteral("id")).toString();
  const auto style_name = string_or_fallback(style, QStringLiteral("name"), style_id);
  const auto created_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
  QString text;
  text += QStringLiteral("---\n");
  text += QStringLiteral("type: agent_feedback_memory\n");
  text += schema_version_front_matter();
  text += QStringLiteral("style_id: %1\n").arg(front_matter_string(style_id));
  text += QStringLiteral("style_name: %1\n").arg(front_matter_string(style_name));
  text += QStringLiteral("created_at_utc: %1\n").arg(front_matter_string(created_at));
  text += QStringLiteral("tags:\n");
  text += QStringLiteral("  - agent-memory\n");
  text += QStringLiteral("  - feedback\n");
  text += QStringLiteral("  - style/%1\n").arg(safe_memory_id(style_name, QStringLiteral("unnamed")));
  QStringList links{linked_style_text(style_id)};
  const auto last_run_id = last_run_report.value(QStringLiteral("runId")).toString();
  if (!last_run_id.trimmed().isEmpty()) {
    links.append(linked_note_text_from_id(last_run_id));
  }
  text += front_matter_link_list(QStringLiteral("links"), links);
  text += QStringLiteral("---\n\n");
  text += QStringLiteral("# Feedback for %1\n\n").arg(markdown_escape(style_name));
  text += QStringLiteral("%1\n\n").arg(markdown_escape(feedback));
  text += QStringLiteral("Related style: [[%1]]\n\n")
              .arg(safe_memory_id(style_id, QStringLiteral("style")));
  if (!last_run_report.isEmpty()) {
    text += QStringLiteral("## Last Run\n\n");
    text += QStringLiteral("- Mode: %1\n")
                .arg(markdown_escape(last_run_report.value(QStringLiteral("mode")).toString()));
    text += QStringLiteral("- Run id: %1\n")
                .arg(markdown_escape(last_run_report.value(QStringLiteral("runId")).toString()));
    if (!last_run_id.trimmed().isEmpty()) {
      text += QStringLiteral("- Related run: %1\n").arg(linked_note_text_from_id(last_run_id));
    }
  }
  if (!context.isEmpty()) {
    text += QStringLiteral("\n## Context\n\n```json\n%1\n```\n").arg(json_block(context));
  }
  return text;
}

QString project_note_markdown(const QVariantMap& style, const QVariantMap& project_report) {
  const auto style_id = style.value(QStringLiteral("id")).toString();
  const auto style_name = string_or_fallback(style, QStringLiteral("name"), style_id);
  const auto scope = project_report.value(QStringLiteral("scope")).toString();
  const auto project_id = safe_memory_id(
      QStringLiteral("%1-%2").arg(style_id, scope),
      QStringLiteral("project"));
  const auto asset_ids = compact_text_list(project_report.value(QStringLiteral("assetIds")));
  const auto sample_asset_ids =
      compact_text_list(project_report.value(QStringLiteral("sampleAssetIds")));
  const auto representative_asset_ids =
      compact_text_list(project_report.value(QStringLiteral("representativeAssetIds")));
  QString text;
  text += QStringLiteral("---\n");
  text += QStringLiteral("type: agent_project_memory\n");
  text += schema_version_front_matter();
  text += QStringLiteral("project_id: %1\n").arg(front_matter_string(project_id));
  text += QStringLiteral("style_id: %1\n").arg(front_matter_string(style_id));
  text += QStringLiteral("style_name: %1\n").arg(front_matter_string(style_name));
  text += QStringLiteral("scope: %1\n").arg(front_matter_string(scope));
  text += QStringLiteral("updated_at_utc: %1\n")
              .arg(front_matter_string(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)));
  text += QStringLiteral("tags:\n");
  text += QStringLiteral("  - agent-memory\n");
  text += QStringLiteral("  - project\n");
  text += QStringLiteral("  - style/%1\n").arg(safe_memory_id(style_name, QStringLiteral("unnamed")));
  text += front_matter_link_list(QStringLiteral("links"), QStringList{linked_style_text(style_id)});
  text += QStringLiteral("---\n\n");
  text += QStringLiteral("# Project Plan - %1\n\n").arg(markdown_escape(style_name));
  text += QStringLiteral("Related style: %1\n\n").arg(linked_style_text(style_id));
  text += QStringLiteral("## Project Brief\n\n");
  text += QStringLiteral("- Scope: %1\n").arg(markdown_escape(scope));
  text += QStringLiteral("- Asset count: %1\n").arg(project_report.value(QStringLiteral("assetCount")).toInt());
  text += QStringLiteral("- Cluster count: %1\n").arg(project_report.value(QStringLiteral("clusterCount")).toInt());
  text += QStringLiteral("- Sample first: %1\n")
              .arg(project_report.value(QStringLiteral("sampleFirst")).toBool()
                       ? QStringLiteral("yes")
                       : QStringLiteral("no"));
  text += QStringLiteral("- Validation required: %1\n\n")
              .arg(project_report.value(QStringLiteral("validationRequired")).toBool()
                       ? QStringLiteral("yes")
                       : QStringLiteral("no"));
  const auto plan_summary = project_report.value(QStringLiteral("planSummary")).toString();
  if (!plan_summary.trimmed().isEmpty()) {
    text += QStringLiteral("%1\n\n").arg(markdown_escape(plan_summary));
  }
  text += QStringLiteral("## Sample Assets\n\n");
  text += markdown_list_or_none(sample_asset_ids.isEmpty() ? representative_asset_ids : sample_asset_ids);
  text += QStringLiteral("\n## Asset References\n\n");
  text += markdown_list_or_none(asset_ids);
  text += QStringLiteral("\n## Retrieval Hints\n\n");
  text += QStringLiteral("- Use for sample and batch planning.\n");
  text += QStringLiteral("- Do not include source image bytes or local file paths in model context.\n");
  text += QStringLiteral("\n## Compact Plan JSON\n\n```json\n%1\n```\n")
              .arg(json_block(QVariantMap{
                  {QStringLiteral("scope"), scope},
                  {QStringLiteral("assetCount"), project_report.value(QStringLiteral("assetCount"))},
                  {QStringLiteral("clusterCount"), project_report.value(QStringLiteral("clusterCount"))},
                  {QStringLiteral("representativeAssetIds"),
                   project_report.value(QStringLiteral("representativeAssetIds"))},
                  {QStringLiteral("beforeAfterPairCount"),
                   project_report.value(QStringLiteral("beforeAfterPairCount"))},
                  {QStringLiteral("styleMatchScoring"),
                   project_report.value(QStringLiteral("styleMatchScoring"))},
              }));
  return text;
}

QString run_note_markdown(
    const QString& app_data_location,
    const QVariantMap& style,
    const QVariantMap& run_report) {
  const auto style_id = style.value(QStringLiteral("id")).toString();
  const auto style_name = string_or_fallback(style, QStringLiteral("name"), style_id);
  const auto mode = run_report.value(QStringLiteral("mode")).toString();
  const auto run_id = run_report.value(QStringLiteral("runId")).toString();
  const auto asset_ids = compact_text_list(run_report.value(QStringLiteral("assetIds")));
  const auto warnings = compact_text_list(run_report.value(QStringLiteral("warnings")), 8);
  const auto artifact_references = safe_artifact_references_for_run(app_data_location, run_report);
  const auto tool_calls = run_report.value(QStringLiteral("toolCalls")).toList();
  QStringList tool_names;
  for (const auto& tool_call_value : tool_calls) {
    const auto tool_call = tool_call_value.toMap();
    const auto name = tool_call.value(QStringLiteral("name")).toString().trimmed();
    if (!name.isEmpty() && !tool_names.contains(name)) {
      tool_names.append(name);
    }
    if (tool_names.size() >= 12) {
      break;
    }
  }

  const auto validation_report = run_report.value(QStringLiteral("validationReport")).toMap();
  QString text;
  text += QStringLiteral("---\n");
  text += QStringLiteral("type: agent_run_memory\n");
  text += schema_version_front_matter();
  text += QStringLiteral("run_id: %1\n").arg(front_matter_string(run_id));
  text += QStringLiteral("mode: %1\n").arg(front_matter_string(mode));
  text += QStringLiteral("style_id: %1\n").arg(front_matter_string(style_id));
  text += QStringLiteral("style_name: %1\n").arg(front_matter_string(style_name));
  text += QStringLiteral("created_at_utc: %1\n")
              .arg(front_matter_string(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)));
  text += QStringLiteral("tags:\n");
  text += QStringLiteral("  - agent-memory\n");
  text += QStringLiteral("  - run\n");
  text += QStringLiteral("  - style/%1\n").arg(safe_memory_id(style_name, QStringLiteral("unnamed")));
  text += front_matter_link_list(QStringLiteral("links"), QStringList{linked_style_text(style_id)});
  text += QStringLiteral("---\n\n");
  text += QStringLiteral("# Run %1\n\n")
              .arg(markdown_escape(run_id.trimmed().isEmpty() ? mode : run_id));
  text += QStringLiteral("Related style: %1\n\n").arg(linked_style_text(style_id));
  text += QStringLiteral("## Outcome\n\n");
  text += QStringLiteral("- Mode: %1\n").arg(markdown_escape(mode));
  text += QStringLiteral("- Success: %1\n")
              .arg(run_report.value(QStringLiteral("ok")).toBool()
                       ? QStringLiteral("yes")
                       : QStringLiteral("no"));
  text += QStringLiteral("- Scope: %1\n").arg(markdown_escape(run_report.value(QStringLiteral("scope")).toString()));
  text += QStringLiteral("- Asset count: %1\n")
              .arg(run_report.value(QStringLiteral("assetCount"),
                                    run_report.value(QStringLiteral("targetAssetCount"))).toInt());
  if (!validation_report.isEmpty()) {
    text += QStringLiteral("- Validation passed: %1\n")
                .arg(validation_report.value(QStringLiteral("ok")).toBool()
                         ? QStringLiteral("yes")
                         : QStringLiteral("no"));
  }
  const auto plan_summary = run_report.value(QStringLiteral("planSummary")).toString();
  if (!plan_summary.trimmed().isEmpty()) {
    text += QStringLiteral("\n%1\n").arg(markdown_escape(plan_summary));
  }
  text += QStringLiteral("\n## Asset References\n\n");
  text += markdown_list_or_none(asset_ids);
  text += QStringLiteral("\n## Tools Used\n\n");
  text += markdown_list_or_none(tool_names);
  if (!warnings.isEmpty()) {
    text += QStringLiteral("\n## Warnings\n\n");
    for (const auto& warning : warnings) {
      text += QStringLiteral("- %1\n").arg(markdown_escape(warning));
    }
  }
  if (!artifact_references.isEmpty()) {
    text += QStringLiteral("\n## Safe Proof Links\n\n");
    text += QStringLiteral("Generated proofs are linked as relative app-owned artifacts; source image paths and image bytes are not stored in memory.\n\n");
    for (const auto& reference_value : artifact_references) {
      const auto reference = reference_value.toMap();
      const auto label = reference.value(QStringLiteral("label")).toString();
      const auto href = reference.value(QStringLiteral("markdownHref")).toString();
      const auto relative_path = reference.value(QStringLiteral("relativePath")).toString();
      const auto asset_id = reference.value(QStringLiteral("assetId")).toString();
      text += QStringLiteral("- %1%2: [%3](%4)\n")
                  .arg(
                      markdown_escape(label),
                      asset_id.trimmed().isEmpty()
                          ? QString()
                          : QStringLiteral(" `%1`").arg(markdown_escape(asset_id)),
                      markdown_escape(relative_path),
                      markdown_escape(href));
    }
  }
  text += QStringLiteral("\n## Compact Run JSON\n\n```json\n%1\n```\n")
              .arg(json_block(QVariantMap{
                  {QStringLiteral("mode"), mode},
                  {QStringLiteral("runId"), run_id},
                  {QStringLiteral("scope"), run_report.value(QStringLiteral("scope"))},
                  {QStringLiteral("assetCount"),
                   run_report.value(QStringLiteral("assetCount"),
                                    run_report.value(QStringLiteral("targetAssetCount")))},
                  {QStringLiteral("draftActive"), run_report.value(QStringLiteral("draftActive"))},
                  {QStringLiteral("reviewRequired"), run_report.value(QStringLiteral("reviewRequired"))},
                  {QStringLiteral("mcpToolPhase"), run_report.value(QStringLiteral("mcpToolPhase"))},
                  {QStringLiteral("clusterCount"), run_report.value(QStringLiteral("clusterCount"))},
                  {QStringLiteral("styleConstraintCitationCount"),
                   run_report.value(QStringLiteral("styleConstraintCitations")).toList().size()},
                  {QStringLiteral("safeArtifactReferences"), artifact_references},
              }));
  return text;
}

QString issue_note_markdown(const QVariantMap& style, const QVariantMap& run_report) {
  const auto style_id = style.value(QStringLiteral("id")).toString();
  const auto style_name = string_or_fallback(style, QStringLiteral("name"), style_id);
  const auto mode = run_report.value(QStringLiteral("mode")).toString();
  const auto run_id = run_report.value(QStringLiteral("runId")).toString();
  const auto issue_summary = issue_summary_for_run(run_report);
  const auto warnings = issue_summary.value(QStringLiteral("warnings")).toStringList();
  const auto errors = issue_summary.value(QStringLiteral("errors")).toStringList();
  const auto outlier_asset_ids = issue_summary.value(QStringLiteral("outlierAssetIds")).toStringList();

  QStringList repair_actions;
  for (const auto& tool_call_value : run_report.value(QStringLiteral("toolCalls")).toList()) {
    const auto tool_call = tool_call_value.toMap();
    const auto name = tool_call.value(QStringLiteral("name")).toString();
    if (name.contains(QStringLiteral("repair"), Qt::CaseInsensitive) ||
        name.contains(QStringLiteral("revise"), Qt::CaseInsensitive) ||
        name.contains(QStringLiteral("validate"), Qt::CaseInsensitive)) {
      append_unique_text(&repair_actions, name, 12);
    }
  }

  QString text;
  text += QStringLiteral("---\n");
  text += QStringLiteral("type: agent_issue_memory\n");
  text += schema_version_front_matter();
  text += QStringLiteral("run_id: %1\n").arg(front_matter_string(run_id));
  text += QStringLiteral("mode: %1\n").arg(front_matter_string(mode));
  text += QStringLiteral("style_id: %1\n").arg(front_matter_string(style_id));
  text += QStringLiteral("style_name: %1\n").arg(front_matter_string(style_name));
  text += QStringLiteral("created_at_utc: %1\n")
              .arg(front_matter_string(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)));
  text += QStringLiteral("tags:\n");
  text += QStringLiteral("  - agent-memory\n");
  text += QStringLiteral("  - issue\n");
  text += QStringLiteral("  - validation\n");
  text += QStringLiteral("  - style/%1\n").arg(safe_memory_id(style_name, QStringLiteral("unnamed")));
  text += front_matter_link_list(
      QStringLiteral("links"),
      QStringList{
          linked_style_text(style_id),
          linked_note_text_from_id(run_id.trimmed().isEmpty() ? mode : run_id),
      });
  text += QStringLiteral("---\n\n");
  text += QStringLiteral("# Issues for Run %1\n\n")
              .arg(markdown_escape(run_id.trimmed().isEmpty() ? mode : run_id));
  text += QStringLiteral("Related style: %1\n\n").arg(linked_style_text(style_id));
  text += QStringLiteral("Related run: [[%1]]\n\n")
              .arg(safe_memory_id(run_id.trimmed().isEmpty() ? mode : run_id, QStringLiteral("run")));
  text += QStringLiteral("## Issue Summary\n\n");
  text += QStringLiteral("- Warnings: %1\n").arg(warnings.size());
  text += QStringLiteral("- Errors: %1\n").arg(errors.size());
  text += QStringLiteral("- Outliers: %1\n").arg(outlier_asset_ids.size());
  text += QStringLiteral("- Model repairs: %1\n\n")
              .arg(issue_summary.value(QStringLiteral("modelRepairCount")).toInt());

  if (!warnings.isEmpty()) {
    text += QStringLiteral("## Warnings\n\n");
    for (const auto& warning : warnings.mid(0, 12)) {
      text += QStringLiteral("- %1\n").arg(issue_memory_text(warning));
    }
    text += QLatin1Char('\n');
  }
  if (!errors.isEmpty()) {
    text += QStringLiteral("## Errors\n\n");
    for (const auto& error : errors.mid(0, 8)) {
      text += QStringLiteral("- %1\n").arg(issue_memory_text(error));
    }
    text += QLatin1Char('\n');
  }
  if (!outlier_asset_ids.isEmpty()) {
    text += QStringLiteral("## Outlier Assets\n\n");
    text += markdown_list_or_none(outlier_asset_ids);
    text += QLatin1Char('\n');
  }
  text += QStringLiteral("## Repair Strategy\n\n");
  if (repair_actions.isEmpty()) {
    text += QStringLiteral("- Review validation warnings before applying this style to similar photos.\n");
  } else {
    for (const auto& action : repair_actions) {
      text += QStringLiteral("- `%1`\n").arg(markdown_escape(action));
    }
  }
  text += QStringLiteral("\n## Retrieval Hints\n\n");
  text += QStringLiteral("- Use before outlier repair or similar validation warnings.\n");
  text += QStringLiteral("- Keep the original run artifacts separate from durable memory.\n");
  text += QStringLiteral("\n## Compact Issue JSON\n\n```json\n%1\n```\n")
              .arg(json_block(QVariantMap{
                  {QStringLiteral("runId"), run_id},
                  {QStringLiteral("mode"), mode},
                  {QStringLiteral("warningCount"), warnings.size()},
                  {QStringLiteral("errorCount"), errors.size()},
                  {QStringLiteral("outlierAssetIds"), outlier_asset_ids},
                  {QStringLiteral("modelRepairCount"),
                   issue_summary.value(QStringLiteral("modelRepairCount"))},
                  {QStringLiteral("validationOk"), issue_summary.value(QStringLiteral("validationOk"))},
              }));
  return text;
}

QString tool_note_markdown(const QVariantMap& style) {
  const auto style_id = style.value(QStringLiteral("id")).toString();
  const auto style_name = string_or_fallback(style, QStringLiteral("name"), style_id);
  const auto allowed_tools = allowed_tools_map(style);
  const auto tool_knowledge = tool_knowledge_map(style);
  QString text;
  text += QStringLiteral("---\n");
  text += QStringLiteral("type: agent_tool_memory\n");
  text += schema_version_front_matter();
  text += QStringLiteral("style_id: %1\n").arg(front_matter_string(style_id));
  text += QStringLiteral("style_name: %1\n").arg(front_matter_string(style_name));
  text += QStringLiteral("updated_at_utc: %1\n")
              .arg(front_matter_string(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)));
  text += QStringLiteral("tags:\n");
  text += QStringLiteral("  - agent-memory\n");
  text += QStringLiteral("  - tool\n");
  text += QStringLiteral("  - rulebook\n");
  text += QStringLiteral("  - style/%1\n").arg(safe_memory_id(style_name, QStringLiteral("unnamed")));
  text += front_matter_link_list(QStringLiteral("links"), QStringList{linked_style_text(style_id)});
  text += QStringLiteral("---\n\n");
  text += QStringLiteral("# Tool Rulebook - %1\n\n").arg(markdown_escape(style_name));
  text += QStringLiteral("Related style: %1\n\n").arg(linked_style_text(style_id));
  text += QStringLiteral("## Tool Rules\n\n");
  if (tool_knowledge.isEmpty()) {
    text += QStringLiteral("- No tool rules are recorded yet.\n");
  } else {
    for (auto iterator = tool_knowledge.cbegin(); iterator != tool_knowledge.cend(); ++iterator) {
      const auto key = iterator.key();
      text += QStringLiteral("### %1\n\n").arg(markdown_escape(key));
      text += QStringLiteral("- Policy: %1\n").arg(markdown_escape(tool_policy_label(allowed_tools, key)));
      text += QStringLiteral("- Rule: %1\n\n").arg(markdown_escape(iterator.value().toString()));
    }
  }
  text += QStringLiteral("## Agent Use\n\n");
  text += QStringLiteral("- Choose the smallest tool set that can solve the photo problem.\n");
  text += QStringLiteral("- Prefer global tone/color before masks unless local separation is necessary.\n");
  text += QStringLiteral("- Treat retouching, object removal, and aggressive crop changes as review-required.\n");
  text += QStringLiteral("- Use phase-specific MCP tool policy before calling editor tools.\n");
  text += QStringLiteral("\n## Compact Tool JSON\n\n```json\n%1\n```\n")
              .arg(json_block(QVariantMap{
                  {QStringLiteral("styleId"), style_id},
                  {QStringLiteral("toolCount"), tool_knowledge.size()},
                  {QStringLiteral("tools"), compact_tool_cards_for_style(style)},
              }));
  return text;
}

QVariantMap refresh_style_memory_backlinks(
    const QString& app_data_location,
    const QVariantMap& style) {
  const auto style_id = style.value(QStringLiteral("id")).toString();
  if (style_id.trimmed().isEmpty()) {
    return QVariantMap{
        {QStringLiteral("ok"), true},
        {QStringLiteral("skipped"), true},
        {QStringLiteral("reason"), QStringLiteral("No style id is available for memory backlink refresh.")},
    };
  }
  const auto path = agent_memory_style_note_path(app_data_location, style_id);
  QString error_message;
  const auto ok = write_text_file(path, style_note_markdown(app_data_location, style), &error_message);
  QVariantMap report{
      {QStringLiteral("ok"), ok},
      {QStringLiteral("type"), QStringLiteral("style-memory-backlink-refresh")},
      {QStringLiteral("path"), path},
      {QStringLiteral("styleId"), style_id},
  };
  if (!ok) {
    report.insert(QStringLiteral("errors"), QStringList{error_message});
  }
  return report;
}

}  // namespace

QString agent_memory_root_directory(const QString& app_data_location) {
  return QDir(app_data_location).filePath(QStringLiteral("agent-memory"));
}

QString agent_memory_style_note_path(const QString& app_data_location, const QString& style_id) {
  return QDir(agent_memory_root_directory(app_data_location))
      .filePath(QStringLiteral("styles/%1.md")
                    .arg(safe_memory_id(style_id, QStringLiteral("style"))));
}

QString agent_memory_project_note_path(
    const QString& app_data_location,
    const QString& style_id,
    const QString& scope) {
  return QDir(agent_memory_root_directory(app_data_location))
      .filePath(QStringLiteral("projects/%1.md")
                    .arg(safe_memory_id(
                        QStringLiteral("%1-%2").arg(style_id, scope),
                        QStringLiteral("project"))));
}

QString agent_memory_run_note_path(
    const QString& app_data_location,
    const QString& run_id,
    const QString& mode) {
  const auto base_id = run_id.trimmed().isEmpty()
                           ? QStringLiteral("%1-%2").arg(memory_timestamp_id(QStringLiteral("run")), mode)
                           : run_id;
  return QDir(agent_memory_root_directory(app_data_location))
      .filePath(QStringLiteral("runs/%1.md").arg(safe_memory_id(base_id, QStringLiteral("run"))));
}

QString agent_memory_issue_note_path(
    const QString& app_data_location,
    const QString& run_id,
    const QString& mode) {
  const auto base_id = run_id.trimmed().isEmpty()
                           ? QStringLiteral("%1-%2").arg(memory_timestamp_id(QStringLiteral("issue")), mode)
                           : QStringLiteral("%1-issues").arg(run_id);
  return QDir(agent_memory_root_directory(app_data_location))
      .filePath(QStringLiteral("issues/%1.md").arg(safe_memory_id(base_id, QStringLiteral("issue"))));
}

QString agent_memory_tool_note_path(
    const QString& app_data_location,
    const QString& style_id) {
  return QDir(agent_memory_root_directory(app_data_location))
      .filePath(QStringLiteral("tools/%1-tool-rulebook.md")
                    .arg(safe_memory_id(style_id, QStringLiteral("style"))));
}

QVariantMap agent_memory_vault_status(const QString& app_data_location) {
  const auto root_path = agent_memory_root_directory(app_data_location);
  const QDir root(root_path);
  const QStringList sections{
      QStringLiteral("styles"),
      QStringLiteral("projects"),
      QStringLiteral("feedback"),
      QStringLiteral("runs"),
      QStringLiteral("tools"),
      QStringLiteral("issues"),
  };
  QVariantMap note_counts;
  QVariantList section_reports;
  auto total_notes = 0;
  for (const auto& section : sections) {
    const auto count = markdown_note_count(root, section);
    total_notes += count;
    note_counts.insert(section, count);
    section_reports.append(QVariantMap{
        {QStringLiteral("name"), section},
        {QStringLiteral("path"), root.filePath(section)},
        {QStringLiteral("noteCount"), count},
    });
  }
  const auto schema_status = agent_memory_schema_status(root_path);

  QVariantMap status{
      {QStringLiteral("ok"), true},
      {QStringLiteral("mode"), QStringLiteral("agent-memory-vault-status")},
      {QStringLiteral("rootPath"), root_path},
      {QStringLiteral("exists"), root.exists()},
      {QStringLiteral("format"), QStringLiteral("markdown-yaml-frontmatter")},
      {QStringLiteral("obsidianCompatible"), true},
      {QStringLiteral("localFirst"), true},
      {QStringLiteral("humanReadable"), true},
      {QStringLiteral("storesImageBytes"), false},
      {QStringLiteral("userManagedDuringNormalEditing"), false},
      {QStringLiteral("contextStrategy"), QStringLiteral("retrieved-packets-not-raw-vault")},
      {QStringLiteral("schemaVersion"), schema_status.value(QStringLiteral("schemaVersion"))},
      {QStringLiteral("schemaVersionOk"), schema_status.value(QStringLiteral("schemaVersionOk"))},
      {QStringLiteral("schemaCheckedNoteCount"),
       schema_status.value(QStringLiteral("schemaCheckedNoteCount"))},
      {QStringLiteral("schemaMismatchCount"), schema_status.value(QStringLiteral("schemaMismatchCount"))},
      {QStringLiteral("schemaMismatches"), schema_status.value(QStringLiteral("schemaMismatches"))},
      {QStringLiteral("migrationRequired"), schema_status.value(QStringLiteral("migrationRequired"))},
      {QStringLiteral("migrationStrategy"), schema_status.value(QStringLiteral("migrationStrategy"))},
      {QStringLiteral("noteCounts"), note_counts},
      {QStringLiteral("sections"), section_reports},
      {QStringLiteral("totalNoteCount"), total_notes},
      {QStringLiteral("defaultSectionNames"), sections},
  };
  return status;
}

QVariantMap agent_memory_vault_snapshot(const QString& app_data_location) {
  const auto root_path = agent_memory_root_directory(app_data_location);
  const QDir root(root_path);
  QStringList absolute_paths;
  if (root.exists()) {
    QDirIterator iterator(
        root_path,
        QStringList{QStringLiteral("*.md")},
        QDir::Files,
        QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
      absolute_paths.append(iterator.next());
    }
  }
  std::sort(absolute_paths.begin(), absolute_paths.end());

  QVariantList files;
  QCryptographicHash vault_hash(QCryptographicHash::Sha256);
  for (const auto& absolute_path : absolute_paths) {
    const QFileInfo info(absolute_path);
    const auto relative_path = root.relativeFilePath(absolute_path);
    QFile file(absolute_path);
    QByteArray bytes;
    if (file.open(QIODevice::ReadOnly)) {
      bytes = file.readAll();
    }

    QCryptographicHash file_hash(QCryptographicHash::Sha256);
    file_hash.addData(bytes);
    const auto content_hash = QString::fromLatin1(file_hash.result().toHex());
    const auto modified_utc = info.lastModified().toUTC().toString(Qt::ISODateWithMs);
    const QVariantMap file_state{
        {QStringLiteral("relativePath"), relative_path},
        {QStringLiteral("sizeBytes"), info.size()},
        {QStringLiteral("lastModifiedUtc"), modified_utc},
        {QStringLiteral("contentHash"), content_hash},
    };
    files.append(file_state);

    vault_hash.addData(relative_path.toUtf8());
    vault_hash.addData(QByteArrayLiteral("\n"));
    vault_hash.addData(QByteArray::number(info.size()));
    vault_hash.addData(QByteArrayLiteral("\n"));
    vault_hash.addData(content_hash.toUtf8());
    vault_hash.addData(QByteArrayLiteral("\n"));
  }

  return QVariantMap{
      {QStringLiteral("ok"), true},
      {QStringLiteral("mode"), QStringLiteral("agent-memory-vault-snapshot")},
      {QStringLiteral("rootPath"), root_path},
      {QStringLiteral("exists"), root.exists()},
      {QStringLiteral("noteCount"), files.size()},
      {QStringLiteral("fingerprint"), QString::fromLatin1(vault_hash.result().toHex())},
      {QStringLiteral("files"), files},
      {QStringLiteral("createdAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
  };
}

QVariantMap agent_memory_continuity_state(const QString& app_data_location) {
  const auto path = QDir(agent_memory_root_directory(app_data_location))
                        .filePath(QStringLiteral("continuity.json"));
  QFile file(path);
  if (!file.exists()) {
    return QVariantMap{
        {QStringLiteral("ok"), true},
        {QStringLiteral("path"), path},
        {QStringLiteral("version"), 1},
        {QStringLiteral("projects"), QVariantMap{}},
    };
  }
  if (!file.open(QIODevice::ReadOnly)) {
    return QVariantMap{
        {QStringLiteral("ok"), false},
        {QStringLiteral("path"), path},
        {QStringLiteral("errors"),
         QStringList{QStringLiteral("Unable to read agent continuity state: %1").arg(path)}},
    };
  }
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(file.readAll(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    return QVariantMap{
        {QStringLiteral("ok"), false},
        {QStringLiteral("path"), path},
        {QStringLiteral("errors"),
         QStringList{QStringLiteral("Agent continuity state is invalid JSON at offset %1: %2")
                         .arg(parse_error.offset)
                         .arg(parse_error.errorString())}},
    };
  }
  auto state = document.object().toVariantMap();
  state.insert(QStringLiteral("ok"), true);
  state.insert(QStringLiteral("path"), path);
  if (!state.contains(QStringLiteral("version"))) {
    state.insert(QStringLiteral("version"), 1);
  }
  if (!state.contains(QStringLiteral("projects"))) {
    state.insert(QStringLiteral("projects"), QVariantMap{});
  }
  return state;
}

QVariantMap write_agent_memory_continuity_state(
    const QString& app_data_location,
    const QVariantMap& continuity_state) {
  const auto root_path = agent_memory_root_directory(app_data_location);
  const auto path = QDir(root_path).filePath(QStringLiteral("continuity.json"));
  if (!QDir().mkpath(root_path)) {
    return QVariantMap{
        {QStringLiteral("ok"), false},
        {QStringLiteral("path"), path},
        {QStringLiteral("errors"),
         QStringList{QStringLiteral("Unable to create agent memory directory: %1").arg(root_path)}},
    };
  }
  auto state = continuity_state;
  state.remove(QStringLiteral("ok"));
  state.remove(QStringLiteral("path"));
  state.insert(QStringLiteral("version"), 1);
  state.insert(
      QStringLiteral("updatedAtUtc"),
      QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return QVariantMap{
        {QStringLiteral("ok"), false},
        {QStringLiteral("path"), path},
        {QStringLiteral("errors"),
         QStringList{QStringLiteral("Unable to write agent continuity state: %1").arg(path)}},
    };
  }
  const auto bytes =
      QJsonDocument(QJsonObject::fromVariantMap(state)).toJson(QJsonDocument::Indented);
  if (file.write(bytes) != bytes.size() || !file.commit()) {
    return QVariantMap{
        {QStringLiteral("ok"), false},
        {QStringLiteral("path"), path},
        {QStringLiteral("errors"),
         QStringList{QStringLiteral("Unable to commit agent continuity state: %1").arg(path)}},
    };
  }
  return QVariantMap{
      {QStringLiteral("ok"), true},
      {QStringLiteral("path"), path},
      {QStringLiteral("version"), 1},
      {QStringLiteral("projectCount"), state.value(QStringLiteral("projects")).toMap().size()},
  };
}

QVariantMap export_agent_memory_vault(
    const QString& app_data_location,
    const QString& destination_directory,
    bool redacted) {
  const auto root_path = agent_memory_root_directory(app_data_location);
  const QDir root(root_path);
  if (!root.exists()) {
    return QVariantMap{
        {QStringLiteral("ok"), false},
        {QStringLiteral("mode"), QStringLiteral("agent-memory-vault-export")},
        {QStringLiteral("errors"), QStringList{QStringLiteral("Agent memory vault does not exist.")}},
    };
  }

  const auto export_id = QStringLiteral("agent-memory-%1%2")
                             .arg(memory_timestamp_id(QStringLiteral("export")),
                                  redacted ? QStringLiteral("-redacted") : QString());
  const auto base_directory = destination_directory.trimmed().isEmpty()
                                  ? QDir(app_data_location).filePath(QStringLiteral("agent-memory-exports"))
                                  : destination_directory.trimmed();
  const auto export_path = QDir(base_directory).filePath(export_id);
  if (!QDir().mkpath(export_path)) {
    return QVariantMap{
        {QStringLiteral("ok"), false},
        {QStringLiteral("mode"), QStringLiteral("agent-memory-vault-export")},
        {QStringLiteral("errors"),
         QStringList{QStringLiteral("Unable to create memory export directory: %1").arg(export_path)}},
    };
  }

  QStringList errors;
  QVariantList exported_files;
  auto note_count = 0;
  QDirIterator iterator(root_path, QStringList{QStringLiteral("*.md")}, QDir::Files, QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    const auto source_path = iterator.next();
    const auto relative_path = root.relativeFilePath(source_path);
    const auto target_path = QDir(export_path).filePath(relative_path);
    const QFileInfo target_info(target_path);
    if (!QDir().mkpath(target_info.dir().absolutePath())) {
      errors.append(QStringLiteral("Unable to create export subdirectory: %1")
                        .arg(target_info.dir().absolutePath()));
      continue;
    }

    QFile source_file(source_path);
    if (!source_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      errors.append(QStringLiteral("Unable to read memory note: %1").arg(source_path));
      continue;
    }
    auto text = QString::fromUtf8(source_file.readAll());
    if (redacted) {
      text = redacted_memory_text(text, app_data_location);
    }

    QString write_error;
    if (!write_text_file(target_path, text, &write_error)) {
      errors.append(write_error);
      continue;
    }
    ++note_count;
    exported_files.append(relative_path);
  }

  const QVariantMap manifest{
      {QStringLiteral("type"), QStringLiteral("agent_memory_vault_export")},
      {QStringLiteral("memorySchemaVersion"), kAgentMemorySchemaVersion},
      {QStringLiteral("createdAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
      {QStringLiteral("redacted"), redacted},
      {QStringLiteral("sourceVaultIncluded"), false},
      {QStringLiteral("storesImageBytes"), false},
      {QStringLiteral("noteCount"), note_count},
      {QStringLiteral("files"), exported_files},
  };
  QString manifest_error;
  if (!write_text_file(
          QDir(export_path).filePath(QStringLiteral("manifest.json")),
          QString::fromUtf8(QJsonDocument::fromVariant(manifest).toJson(QJsonDocument::Indented)),
          &manifest_error)) {
    errors.append(manifest_error);
  }

  QVariantMap report{
      {QStringLiteral("ok"), errors.isEmpty()},
      {QStringLiteral("mode"), QStringLiteral("agent-memory-vault-export")},
      {QStringLiteral("exportPath"), export_path},
      {QStringLiteral("memorySchemaVersion"), kAgentMemorySchemaVersion},
      {QStringLiteral("redacted"), redacted},
      {QStringLiteral("noteCount"), note_count},
      {QStringLiteral("manifestPath"), QDir(export_path).filePath(QStringLiteral("manifest.json"))},
      {QStringLiteral("files"), exported_files},
  };
  if (!errors.isEmpty()) {
    report.insert(QStringLiteral("errors"), errors);
  }
  return report;
}

QVariantMap reset_agent_memory_vault(const QString& app_data_location, bool confirmed) {
  const auto root_path = agent_memory_root_directory(app_data_location);
  if (!confirmed) {
    return QVariantMap{
        {QStringLiteral("ok"), false},
        {QStringLiteral("mode"), QStringLiteral("agent-memory-vault-reset")},
        {QStringLiteral("requiresConfirmation"), true},
        {QStringLiteral("errors"),
         QStringList{QStringLiteral("Confirm before resetting the local agent memory vault.")}},
    };
  }
  QDir root(root_path);
  const auto existed = root.exists();
  const auto removed_count = recursive_file_count(root_path);
  if (existed && !root.removeRecursively()) {
    return QVariantMap{
        {QStringLiteral("ok"), false},
        {QStringLiteral("mode"), QStringLiteral("agent-memory-vault-reset")},
        {QStringLiteral("rootPath"), root_path},
        {QStringLiteral("errors"),
         QStringList{QStringLiteral("Unable to reset agent memory vault: %1").arg(root_path)}},
    };
  }
  return QVariantMap{
      {QStringLiteral("ok"), true},
      {QStringLiteral("mode"), QStringLiteral("agent-memory-vault-reset")},
      {QStringLiteral("rootPath"), root_path},
      {QStringLiteral("existed"), existed},
      {QStringLiteral("removedFileCount"), removed_count},
  };
}

QVariantMap write_agent_style_memory_note(
    const QString& app_data_location,
    const QVariantMap& style) {
  const auto style_id = style.value(QStringLiteral("id")).toString();
  const auto path = agent_memory_style_note_path(app_data_location, style_id);
  QString error_message;
  const auto ok = write_text_file(path, style_note_markdown(app_data_location, style), &error_message);
  QVariantMap report{
      {QStringLiteral("ok"), ok},
      {QStringLiteral("type"), QStringLiteral("style-memory-note")},
      {QStringLiteral("path"), path},
      {QStringLiteral("styleId"), style_id},
      {QStringLiteral("memorySchemaVersion"), kAgentMemorySchemaVersion},
      {QStringLiteral("format"), QStringLiteral("markdown-yaml-frontmatter")},
      {QStringLiteral("storesImageBytes"), false},
  };
  if (!ok) {
    report.insert(QStringLiteral("errors"), QStringList{error_message});
  }
  return report;
}

QVariantMap write_agent_project_memory_note(
    const QString& app_data_location,
    const QVariantMap& style,
    const QVariantMap& project_report) {
  const auto style_id = style.value(QStringLiteral("id")).toString();
  const auto scope = project_report.value(QStringLiteral("scope")).toString();
  const auto path = agent_memory_project_note_path(app_data_location, style_id, scope);
  QString error_message;
  const auto ok = write_text_file(path, project_note_markdown(style, project_report), &error_message);
  const auto backlink_report = ok
                                   ? refresh_style_memory_backlinks(app_data_location, style)
                                   : QVariantMap{};
  QVariantMap report{
      {QStringLiteral("ok"), ok},
      {QStringLiteral("type"), QStringLiteral("project-memory-note")},
      {QStringLiteral("path"), path},
      {QStringLiteral("styleId"), style_id},
      {QStringLiteral("scope"), scope},
      {QStringLiteral("memorySchemaVersion"), kAgentMemorySchemaVersion},
      {QStringLiteral("format"), QStringLiteral("markdown-yaml-frontmatter")},
      {QStringLiteral("storesImageBytes"), false},
  };
  if (!backlink_report.isEmpty()) {
    report.insert(QStringLiteral("styleBacklinkRefresh"), backlink_report);
  }
  if (!ok) {
    report.insert(QStringLiteral("errors"), QStringList{error_message});
  }
  return report;
}

QVariantMap write_agent_feedback_memory_note(
    const QString& app_data_location,
    const QVariantMap& style,
    const QString& feedback,
    const QVariantMap& context,
    const QVariantMap& last_run_report) {
  const auto style_id = style.value(QStringLiteral("id")).toString();
  const auto timestamp =
      safe_memory_id(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz")),
                     QStringLiteral("feedback"));
  const auto path = QDir(agent_memory_root_directory(app_data_location))
                        .filePath(QStringLiteral("feedback/%1-%2.md")
                                      .arg(timestamp, safe_memory_id(style_id, QStringLiteral("style"))));
  QString error_message;
  const auto ok = write_text_file(
      path,
      feedback_note_markdown(style, feedback, context, last_run_report),
      &error_message);
  const auto backlink_report = ok
                                   ? refresh_style_memory_backlinks(app_data_location, style)
                                   : QVariantMap{};
  QVariantMap report{
      {QStringLiteral("ok"), ok},
      {QStringLiteral("type"), QStringLiteral("feedback-memory-note")},
      {QStringLiteral("path"), path},
      {QStringLiteral("styleId"), style_id},
      {QStringLiteral("memorySchemaVersion"), kAgentMemorySchemaVersion},
      {QStringLiteral("format"), QStringLiteral("markdown-yaml-frontmatter")},
      {QStringLiteral("storesImageBytes"), false},
  };
  if (!backlink_report.isEmpty()) {
    report.insert(QStringLiteral("styleBacklinkRefresh"), backlink_report);
  }
  if (!ok) {
    report.insert(QStringLiteral("errors"), QStringList{error_message});
  }
  return report;
}

QVariantMap write_agent_run_memory_note(
    const QString& app_data_location,
    const QVariantMap& style,
    const QVariantMap& run_report) {
  const auto mode = run_report.value(QStringLiteral("mode")).toString();
  const auto run_id = run_report.value(QStringLiteral("runId")).toString();
  const auto path = agent_memory_run_note_path(app_data_location, run_id, mode);
  QString error_message;
  const auto ok = write_text_file(
      path,
      run_note_markdown(app_data_location, style, run_report),
      &error_message);
  const auto backlink_report = ok
                                   ? refresh_style_memory_backlinks(app_data_location, style)
                                   : QVariantMap{};
  QVariantMap report{
      {QStringLiteral("ok"), ok},
      {QStringLiteral("type"), QStringLiteral("run-memory-note")},
      {QStringLiteral("path"), path},
      {QStringLiteral("styleId"), style.value(QStringLiteral("id")).toString()},
      {QStringLiteral("runId"), run_id},
      {QStringLiteral("mode"), mode},
      {QStringLiteral("memorySchemaVersion"), kAgentMemorySchemaVersion},
      {QStringLiteral("format"), QStringLiteral("markdown-yaml-frontmatter")},
      {QStringLiteral("storesImageBytes"), false},
  };
  if (!backlink_report.isEmpty()) {
    report.insert(QStringLiteral("styleBacklinkRefresh"), backlink_report);
  }
  if (!ok) {
    report.insert(QStringLiteral("errors"), QStringList{error_message});
  }
  return report;
}

QVariantMap write_agent_issue_memory_note(
    const QString& app_data_location,
    const QVariantMap& style,
    const QVariantMap& run_report) {
  const auto issue_summary = issue_summary_for_run(run_report);
  if (!issue_summary.value(QStringLiteral("hasIssues")).toBool()) {
    return QVariantMap{
        {QStringLiteral("ok"), true},
      {QStringLiteral("type"), QStringLiteral("issue-memory-note")},
      {QStringLiteral("skipped"), true},
      {QStringLiteral("memorySchemaVersion"), kAgentMemorySchemaVersion},
      {QStringLiteral("reason"), QStringLiteral("No validation issues were found for durable memory.")},
        {QStringLiteral("storesImageBytes"), false},
    };
  }
  const auto mode = run_report.value(QStringLiteral("mode")).toString();
  const auto run_id = run_report.value(QStringLiteral("runId")).toString();
  const auto path = agent_memory_issue_note_path(app_data_location, run_id, mode);
  QString error_message;
  const auto ok = write_text_file(path, issue_note_markdown(style, run_report), &error_message);
  const auto backlink_report = ok
                                   ? refresh_style_memory_backlinks(app_data_location, style)
                                   : QVariantMap{};
  QVariantMap report{
      {QStringLiteral("ok"), ok},
      {QStringLiteral("type"), QStringLiteral("issue-memory-note")},
      {QStringLiteral("path"), path},
      {QStringLiteral("styleId"), style.value(QStringLiteral("id")).toString()},
      {QStringLiteral("runId"), run_id},
      {QStringLiteral("mode"), mode},
      {QStringLiteral("issueSummary"), issue_summary},
      {QStringLiteral("memorySchemaVersion"), kAgentMemorySchemaVersion},
      {QStringLiteral("format"), QStringLiteral("markdown-yaml-frontmatter")},
      {QStringLiteral("storesImageBytes"), false},
  };
  if (!backlink_report.isEmpty()) {
    report.insert(QStringLiteral("styleBacklinkRefresh"), backlink_report);
  }
  if (!ok) {
    report.insert(QStringLiteral("errors"), QStringList{error_message});
  }
  return report;
}

QVariantMap write_agent_tool_memory_note(
    const QString& app_data_location,
    const QVariantMap& style) {
  const auto style_id = style.value(QStringLiteral("id")).toString();
  const auto path = agent_memory_tool_note_path(app_data_location, style_id);
  QString error_message;
  const auto ok = write_text_file(path, tool_note_markdown(style), &error_message);
  const auto backlink_report = ok
                                   ? refresh_style_memory_backlinks(app_data_location, style)
                                   : QVariantMap{};
  QVariantMap report{
      {QStringLiteral("ok"), ok},
      {QStringLiteral("type"), QStringLiteral("tool-memory-note")},
      {QStringLiteral("path"), path},
      {QStringLiteral("styleId"), style_id},
      {QStringLiteral("toolCount"), tool_knowledge_map(style).size()},
      {QStringLiteral("memorySchemaVersion"), kAgentMemorySchemaVersion},
      {QStringLiteral("format"), QStringLiteral("markdown-yaml-frontmatter")},
      {QStringLiteral("storesImageBytes"), false},
  };
  if (!backlink_report.isEmpty()) {
    report.insert(QStringLiteral("styleBacklinkRefresh"), backlink_report);
  }
  if (!ok) {
    report.insert(QStringLiteral("errors"), QStringList{error_message});
  }
  return report;
}

QVariantMap agent_memory_packet_for_phase(
    const QString& app_data_location,
    const QVariantMap& style,
    const QVariantMap& last_run_report,
    const QString& phase) {
  const auto learned_profile =
      map_value(style, QStringLiteral("learnedProfile"), QStringLiteral("learned_profile"));
  const auto style_id = style.value(QStringLiteral("id")).toString();
  const auto normalized_phase = safe_memory_id(phase, QStringLiteral("sample"));
  const auto retrieval_plan = retrieval_plan_for_phase(normalized_phase);
  const auto included_memory_kinds = included_memory_kinds_from_plan(retrieval_plan);
  QVariantMap style_card{
      {QStringLiteral("styleId"), style_id},
      {QStringLiteral("styleName"), style.value(QStringLiteral("name"))},
      {QStringLiteral("goal"), style.value(QStringLiteral("goal"))},
      {QStringLiteral("styleSummary"), learned_profile.value(QStringLiteral("styleSummary"))},
      {QStringLiteral("sourceAssetCount"), learned_profile.value(QStringLiteral("source_asset_count"))},
      {QStringLiteral("usableRecipeCount"), learned_profile.value(QStringLiteral("usable_recipe_count"))},
      {QStringLiteral("latestFeedback"), learned_profile.value(QStringLiteral("latestFeedback"))},
      {QStringLiteral("consistencyGuidelines"),
       learned_profile.value(QStringLiteral("consistencyGuidelines"))},
  };
  QVariantMap tool_card{
      {QStringLiteral("toolCount"), tool_knowledge_map(style).size()},
      {QStringLiteral("tools"), compact_tool_cards_for_style(style, 8)},
      {QStringLiteral("toolMemoryNotePath"), agent_memory_tool_note_path(app_data_location, style_id)},
  };
  QVariantMap style_constraints_card{
      {QStringLiteral("available"), !style.isEmpty()},
      {QStringLiteral("allowedTools"), allowed_tools_map(style)},
      {QStringLiteral("constraints"), map_value(style, QStringLiteral("constraints"), QStringLiteral("constraints"))},
      {QStringLiteral("validation"), map_value(style, QStringLiteral("validation"), QStringLiteral("validation"))},
      {QStringLiteral("batchBehavior"), map_value(style, QStringLiteral("batchBehavior"), QStringLiteral("batch_behavior"))},
  };
  const auto approved_recipe =
      last_run_report.value(QStringLiteral("recipePatch"),
                            last_run_report.value(QStringLiteral("plannedRecipePatch"))).toMap();
  QVariantMap approved_sample_recipe_card{
      {QStringLiteral("available"), !approved_recipe.isEmpty()},
      {QStringLiteral("runId"), last_run_report.value(QStringLiteral("runId"))},
      {QStringLiteral("scope"), last_run_report.value(QStringLiteral("scope"))},
      {QStringLiteral("validationOk"),
       last_run_report.value(QStringLiteral("validationReport")).toMap().value(QStringLiteral("ok"))},
      {QStringLiteral("recipePatch"), approved_recipe},
  };
  QVariantMap cluster_summary_card{
      {QStringLiteral("available"), !last_run_report.value(QStringLiteral("batchClusters")).toList().isEmpty()},
      {QStringLiteral("clusterCount"), last_run_report.value(QStringLiteral("clusterCount"))},
      {QStringLiteral("clusters"), limited_variant_list(last_run_report.value(QStringLiteral("batchClusters")), 6)},
      {QStringLiteral("representativeAssetIds"), last_run_report.value(QStringLiteral("representativeAssetIds"))},
  };
  QVariantMap packet{
      {QStringLiteral("ok"), !style_id.trimmed().isEmpty()},
      {QStringLiteral("phase"), normalized_phase},
      {QStringLiteral("strategy"), QStringLiteral("staged-memory-retrieval")},
      {QStringLiteral("retrievalPlan"), retrieval_plan},
      {QStringLiteral("includedMemoryKinds"), included_memory_kinds},
      {QStringLiteral("memoryBudget"), memory_budget_for_phase(normalized_phase, retrieval_plan)},
      {QStringLiteral("relevancePolicy"), memory_relevance_policy(normalized_phase)},
      {QStringLiteral("styleMemoryNotePath"),
       agent_memory_style_note_path(app_data_location, style_id)},
      {QStringLiteral("toolMemoryNotePath"),
       agent_memory_tool_note_path(app_data_location, style_id)},
      {QStringLiteral("styleCard"), style_card},
      {QStringLiteral("toolCard"), tool_card},
      {QStringLiteral("styleConstraintsCard"), style_constraints_card},
      {QStringLiteral("approvedSampleRecipeCard"), approved_sample_recipe_card},
      {QStringLiteral("clusterSummaryCard"), cluster_summary_card},
      {QStringLiteral("lastRunMode"), last_run_report.value(QStringLiteral("mode"))},
      {QStringLiteral("lastRunId"), last_run_report.value(QStringLiteral("runId"))},
      {QStringLiteral("lastRunCard"),
       QVariantMap{
           {QStringLiteral("mode"), last_run_report.value(QStringLiteral("mode"))},
           {QStringLiteral("runId"), last_run_report.value(QStringLiteral("runId"))},
           {QStringLiteral("ok"), last_run_report.value(QStringLiteral("ok"))},
           {QStringLiteral("scope"), last_run_report.value(QStringLiteral("scope"))},
           {QStringLiteral("assetCount"),
            last_run_report.value(QStringLiteral("assetCount"),
                                  last_run_report.value(QStringLiteral("targetAssetCount")))},
           {QStringLiteral("clusterCount"), last_run_report.value(QStringLiteral("clusterCount"))},
           {QStringLiteral("validationOk"),
            last_run_report.value(QStringLiteral("validationReport")).toMap().value(QStringLiteral("ok"))},
           {QStringLiteral("summary"), last_run_report.value(QStringLiteral("planSummary"))},
       }},
      {QStringLiteral("latestIssueCard"),
       QVariantMap{
           {QStringLiteral("available"),
            last_run_report.value(QStringLiteral("issueMemory")).toMap().value(QStringLiteral("skipped")).toBool()
                ? false
                : last_run_report.contains(QStringLiteral("issueMemory"))},
           {QStringLiteral("runId"), last_run_report.value(QStringLiteral("runId"))},
           {QStringLiteral("path"), last_run_report.value(QStringLiteral("issueMemoryNotePath"))},
           {QStringLiteral("summary"),
            last_run_report.value(QStringLiteral("issueMemory")).toMap().value(QStringLiteral("issueSummary"))},
       }},
      {QStringLiteral("rawVaultIncluded"), false},
  };
  if (normalized_phase == QStringLiteral("setup")) {
    packet.insert(QStringLiteral("ok"), true);
    packet.insert(QStringLiteral("styleCard"), QVariantMap{});
    packet.insert(QStringLiteral("toolCard"), QVariantMap{});
    packet.insert(QStringLiteral("styleConstraintsCard"), QVariantMap{});
    packet.insert(QStringLiteral("approvedSampleRecipeCard"), QVariantMap{});
    packet.insert(QStringLiteral("clusterSummaryCard"), QVariantMap{});
  } else if (style_id.trimmed().isEmpty()) {
    packet.insert(QStringLiteral("errors"), QStringList{QStringLiteral("No style is selected.")});
  }
  return packet;
}

}  // namespace photo_editor::desktop

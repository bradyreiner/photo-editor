#pragma once

#include <QObject>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace photo_editor::desktop {

// Keeps Library navigation choices separate from catalog ownership. Projects,
// assets, imports, and metadata remain owned by their existing catalog/job
// providers; this controller stores only UI preferences and workflow labels.
class LibraryWorkflowController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool navigationCollapsed READ navigationCollapsed WRITE setNavigationCollapsed NOTIFY navigationCollapsedChanged)
  Q_PROPERTY(QString activeDestination READ activeDestination NOTIFY activeDestinationChanged)
  Q_PROPERTY(QVariantList savedSearches READ savedSearches NOTIFY savedSearchesChanged)
  Q_PROPERTY(QVariantMap projectStages READ projectStages NOTIFY projectStagesChanged)
  Q_PROPERTY(QString persistenceError READ persistenceError NOTIFY persistenceErrorChanged)

 public:
  explicit LibraryWorkflowController(
      QString state_file_path,
      QString catalog_scope_key,
      QObject* parent = nullptr);

  [[nodiscard]] bool navigationCollapsed() const { return navigation_collapsed_; }
  void setNavigationCollapsed(bool collapsed);
  [[nodiscard]] QString activeDestination() const { return active_destination_; }
  [[nodiscard]] QVariantList savedSearches() const { return saved_searches_; }
  [[nodiscard]] QVariantMap projectStages() const { return project_stages_; }
  [[nodiscard]] QString persistenceError() const { return persistence_error_; }

  Q_INVOKABLE QString stageForProject(const QString& project_name) const;
  Q_INVOKABLE bool setProjectStage(const QString& project_name, const QString& stage);
  Q_INVOKABLE bool saveSearch(const QString& name, const QVariantMap& filters);
  Q_INVOKABLE bool deleteSearch(const QString& search_id);
  Q_INVOKABLE bool activateSavedSearch(const QString& search_id);
  Q_INVOKABLE void requestAllImages();
  Q_INVOKABLE void requestRecentImports();
  Q_INVOKABLE void requestProject(const QString& project_name);
  Q_INVOKABLE void requestImportSession(const QString& job_id);

 signals:
  void navigationCollapsedChanged();
  void activeDestinationChanged();
  void savedSearchesChanged();
  void projectStagesChanged();
  void persistenceErrorChanged();
  void allImagesRequested();
  void recentImportsRequested();
  void projectRequested(const QString& project_name);
  void importSessionRequested(const QString& job_id);
  void savedSearchRequested(const QString& search_id, const QVariantMap& filters);

 private:
  [[nodiscard]] bool readDocument(QJsonObject* document, QString* error) const;
  [[nodiscard]] bool writeStateField(
      const QString& field,
      const QJsonValue& value,
      QString* error);
  [[nodiscard]] bool writeStateFields(const QJsonObject& fields, QString* error);
  bool activateDestination(const QString& destination);
  void load();
  void setPersistenceError(const QString& error);

  QString state_file_path_;
  QString catalog_scope_key_;
  QString active_destination_ = QStringLiteral("all-images");
  QString persistence_error_;
  bool navigation_collapsed_ = false;
  QVariantMap project_stages_;
  QVariantList saved_searches_;
};

}  // namespace photo_editor::desktop

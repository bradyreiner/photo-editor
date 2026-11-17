#pragma once

#include <QByteArray>
#include <QMutex>
#include <QVector>
#include <QVariantMap>
#include <QString>

#include <functional>
#include <memory>

namespace photo_editor::desktop {

class TimelineDocumentController;

// Pre-task project creation and navigation for native Timeline drafts. These
// operations are not task-journal mutations; callers must inspect after an
// uncertain reply.
class TimelineDraftLifecycle final {
 public:
  using Policy = std::function<bool()>;

  struct ProjectInput final {
    QString input_id;
    QString display_name;
    QString canonical_path;
    QString sha256;
    qint64 byte_size = 0;
  };

  TimelineDraftLifecycle(
      TimelineDocumentController* timeline,
      Policy task_active,
      Policy edit_access,
      Policy activate_timeline_workspace,
      QByteArray project_input_configuration = {});

  [[nodiscard]] QVariantMap invoke(
      const QString& action,
      const QVariantMap& arguments) const;

 private:
  [[nodiscard]] QVariantMap listSavedDrafts() const;
  [[nodiscard]] QVariantMap inspectDocument(bool include_document_hash = true) const;
  [[nodiscard]] QVariantMap listProjectInputs() const;
  [[nodiscard]] QVariantMap importProjectInput(const QVariantMap& arguments) const;
  [[nodiscard]] QVariantMap saveActiveDraft(const QVariantMap& arguments) const;
  [[nodiscard]] QVariantMap closeSavedDraft(const QVariantMap& arguments) const;
  [[nodiscard]] QVariantMap createDraft(const QVariantMap& arguments) const;
  [[nodiscard]] QVariantMap openSavedDraft(const QVariantMap& arguments) const;
  [[nodiscard]] QVariantMap openAutosaveRecovery(const QVariantMap& arguments) const;
  [[nodiscard]] QVariantMap inspectAutosaveRecovery(const QVariantMap& arguments) const;
  [[nodiscard]] QVariantMap activateAfterNavigation(
      QVariantMap result,
      const QString& operation) const;
  [[nodiscard]] QVariantMap autosaveRecoveryStatus(
      const QString& timeline_id,
      const QString& document_hash) const;

  struct AutosaveRecoveryState final {
    QMutex mutex;
    QString candidate_id;
    QString timeline_id;
    QString document_hash;
    bool accepted = false;
    bool terminal = false;
    bool succeeded = false;
    bool workspace_activated = false;
  };

  TimelineDocumentController* timeline_ = nullptr;
  Policy task_active_;
  Policy edit_access_;
  Policy activate_timeline_workspace_;
  QVector<ProjectInput> project_inputs_;
  QString project_input_configuration_error_;
  std::shared_ptr<AutosaveRecoveryState> autosave_recovery_state_;
};

}  // namespace photo_editor::desktop

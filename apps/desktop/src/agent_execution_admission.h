#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include <functional>
#include <utility>

namespace photo_editor::desktop {

// Holds an approved task snapshot until the existing photo/catalog writers
// settle and the exact target snapshot is checked again. This class never
// creates a task journal or performs document writes itself.
class AgentExecutionAdmission final : public QObject {
 public:
  using ReadinessHandler = std::function<QVariantMap(const QVariantMap&)>;
  using SnapshotValidator = std::function<QVariantMap(const QVariantMap&)>;
  using Starter = std::function<void(const QVariantMap&)>;
  using FailureHandler = std::function<void(const QVariantMap&)>;
  using ActivityHandler = std::function<void(const QString&)>;

  AgentExecutionAdmission(
      ReadinessHandler readiness,
      SnapshotValidator validator,
      Starter starter,
      FailureHandler failure,
      ActivityHandler activity = {},
      QObject* parent = nullptr)
      : QObject(parent),
        readiness_(std::move(readiness)),
        validator_(std::move(validator)),
        starter_(std::move(starter)),
        failure_(std::move(failure)),
        activity_(std::move(activity)) {}

  // Admission is always queued. This lets the caller cancel before readiness
  // work or any task writer can run, and prevents re-entrant start ordering.
  [[nodiscard]] QVariantMap submit(const QVariantMap& approved_snapshot) {
    if (pending_ || dispatching_) {
      return {{QStringLiteral("ok"), false},
          {QStringLiteral("pending"), pending_},
          {QStringLiteral("error"), QStringLiteral("A task admission is already in progress.")}};
    }
    if (approved_snapshot.isEmpty()) {
      return {{QStringLiteral("ok"), false},
          {QStringLiteral("error"), QStringLiteral("An approved task snapshot is required.")}};
    }
    if (!readiness_ || !validator_ || !starter_) {
      return {{QStringLiteral("ok"), false},
          {QStringLiteral("error"), QStringLiteral("Task admission is not fully configured.")}};
    }

    ++generation_;
    pending_snapshot_ = approved_snapshot;
    pending_ = true;
    waiting_activity_sent_ = false;
    const auto generation = generation_;
    schedulePoll(generation, 0);
    return {{QStringLiteral("ok"), true},
        {QStringLiteral("pending"), true},
        {QStringLiteral("admissionId"), QVariant::fromValue(generation)}};
  }

  // A pending admission has performed no task writes, so cancel reports that
  // exact boundary. A completed or absent admission cannot be canceled here.
  [[nodiscard]] QVariantMap cancel() {
    if (!pending_) {
      return {{QStringLiteral("ok"), false},
          {QStringLiteral("canceled"), false},
          {QStringLiteral("changed"), false},
          {QStringLiteral("error"), QStringLiteral("There is no pending task admission.")}};
    }
    ++generation_;
    pending_ = false;
    pending_snapshot_.clear();
    waiting_activity_sent_ = false;
    return {{QStringLiteral("ok"), true},
        {QStringLiteral("canceled"), true},
        {QStringLiteral("cancelled"), true},
        {QStringLiteral("canceledBeforeExecution"), true},
        {QStringLiteral("taskStarted"), false},
        {QStringLiteral("noEdits"), true},
        {QStringLiteral("changed"), false},
        {QStringLiteral("status"), QStringLiteral("interrupted")}};
  }

  [[nodiscard]] bool pending() const noexcept { return pending_; }

 private:
  void schedulePoll(quint64 generation, int delay_ms) {
    QTimer::singleShot(delay_ms, this, [this, generation] { poll(generation); });
  }

  void poll(quint64 generation) {
    if (!pending_ || generation != generation_) return;

    const auto readiness = readiness_(pending_snapshot_);
    if (!pending_ || generation != generation_) return;
    const auto error = readiness.value(QStringLiteral("error")).toString().trimmed();
    if (!error.isEmpty()) {
      fail(generation, error, QStringLiteral("readiness"));
      return;
    }
    if (readiness.value(QStringLiteral("pending")).toBool()) {
      if (!waiting_activity_sent_) {
        const auto message = readiness.value(QStringLiteral("activity")).toString().trimmed();
        if (activity_) activity_(message.isEmpty()
            ? QStringLiteral("Waiting for current photo saves to settle.") : message);
        if (!pending_ || generation != generation_) return;
        waiting_activity_sent_ = true;
      }
      schedulePoll(generation, 20);
      return;
    }
    if (!readiness.value(QStringLiteral("ready")).toBool()) {
      fail(generation,
          QStringLiteral("Readiness did not report either a ready state or pending work."),
          QStringLiteral("readiness"));
      return;
    }

    if (activity_)
      activity_(QStringLiteral("Current saves settled; checking the approved targets."));
    if (!pending_ || generation != generation_) return;
    const auto validation = validator_(pending_snapshot_);
    if (!pending_ || generation != generation_) return;
    const auto validation_error = validation.value(QStringLiteral("error")).toString().trimmed();
    if (!validation.value(QStringLiteral("ok")).toBool() || !validation_error.isEmpty()) {
      fail(generation,
          validation_error.isEmpty()
              ? QStringLiteral("The approved targets changed while waiting for photo saves.")
              : validation_error,
          QStringLiteral("snapshot"));
      return;
    }

    // Clear the pending slot before dispatch, but reject re-entrant submissions
    // until the starter has returned. The pinned map is passed unchanged.
    const auto approved_snapshot = pending_snapshot_;
    pending_snapshot_.clear();
    pending_ = false;
    waiting_activity_sent_ = false;
    dispatching_ = true;
    starter_(approved_snapshot);
    dispatching_ = false;
  }

  void fail(quint64 generation, const QString& error, const QString& phase) {
    if (!pending_ || generation != generation_) return;
    ++generation_;
    pending_ = false;
    pending_snapshot_.clear();
    waiting_activity_sent_ = false;
    if (failure_) {
      failure_({{QStringLiteral("ok"), false},
          {QStringLiteral("pending"), false},
          {QStringLiteral("retryable"), true},
          {QStringLiteral("changed"), false},
          {QStringLiteral("phase"), phase},
          {QStringLiteral("error"), error}});
    }
  }

  ReadinessHandler readiness_;
  SnapshotValidator validator_;
  Starter starter_;
  FailureHandler failure_;
  ActivityHandler activity_;
  QVariantMap pending_snapshot_;
  quint64 generation_ = 0;
  bool pending_ = false;
  bool dispatching_ = false;
  bool waiting_activity_sent_ = false;
};

}  // namespace photo_editor::desktop

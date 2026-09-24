#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <functional>

namespace photo_editor::desktop {

class EditorCloseCoordinator final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool closing READ closing NOTIFY stateChanged)
  Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
  Q_PROPERTY(QString error READ error NOTIFY stateChanged)
 public:
  EditorCloseCoordinator(QProcess* helper, std::function<bool(QString*)> checkpoint,
                         std::function<void(bool)> pause, QObject* parent = nullptr);
  void installQuitGuard(QObject* application);
  bool closing() const { return phase_ == Phase::Waiting || phase_ == Phase::Terminating || phase_ == Phase::Killing; }
  bool ready() const { return phase_ == Phase::Ready; }
  QString error() const { return error_; }
  Q_INVOKABLE void requestClose();
  Q_INVOKABLE bool handleWindowClose();
  Q_INVOKABLE void keepEditing();
 signals:
  void stateChanged();
  void closeReady();
 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
 private:
  enum class Phase { Idle, Waiting, Terminating, Killing, Failed, Ready };
  void finish();
  void fail(const QString& message);
  bool checkpoint();
  QPointer<QProcess> helper_;
  QPointer<QObject> quit_target_;
  std::function<bool(QString*)> checkpoint_;
  std::function<void(bool)> pause_;
  QTimer timer_;
  Phase phase_ = Phase::Idle;
  QString error_;
};
}  // namespace photo_editor::desktop

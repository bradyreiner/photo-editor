#pragma once
#include <QObject>

namespace photo_editor::desktop {
class CrashReportingController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool available READ available CONSTANT)
  Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
 public:
  explicit CrashReportingController(QObject* parent = nullptr);
  ~CrashReportingController() override;
  [[nodiscard]] bool available() const;
  [[nodiscard]] bool enabled() const;
  [[nodiscard]] QString status() const { return status_; }
  void setEnabled(bool enabled);
  void setCustomerId(const QString& customer_id);
  // Only fixed event codes are accepted. Never pass raw Qt logs or helper stderr.
  void recordEvent(const QString& event_code);
 signals:
  void changed();
 private:
  bool start();
  void stop();
  bool started_ = false;
  QString customer_id_;
  QString status_;
};
}  // namespace photo_editor::desktop

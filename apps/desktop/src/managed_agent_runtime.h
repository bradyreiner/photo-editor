#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>
#include <QLockFile>
#include <QFutureWatcher>
#include <atomic>
#include <memory>

namespace photo_editor::desktop {

// Owns only its QProcess child. Never attaches to or stops an external service.
class ManagedAgentRuntime final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY changed)
 public:
  explicit ManagedAgentRuntime(QObject* parent = nullptr);
  ~ManagedAgentRuntime() override;
  bool start(const QString& binary, const QString& model_store, quint16 port = 0);
  Q_INVOKABLE void retry();
  Q_INVOKABLE void stop();
  QVariantMap snapshot() const;
  void setModelInstalling(const QString& model, bool installing);
 signals:
  void changed();
 private:
  friend struct ManagedAgentRuntimeTestAccess;
  void publish();
  void fail(const QString& message);
  void poll();
  void readOutput();
  void verifyInstalledModels();
  void acceptModelVerification(QVariantMap result);
  QProcess* process_ = nullptr;
  QPointer<QNetworkReply> reply_;
  QNetworkAccessManager network_;
  QTimer poll_timer_;
  QTimer startup_timer_;
  QString binary_, model_store_, endpoint_, state_ = QStringLiteral("stopped"), error_, version_;
  QString startup_probe_error_;
  QStringList installed_models_;
  QStringList verified_models_;
  QStringList installing_models_;
  QVariantMap model_verification_;
  QFutureWatcher<QVariantMap> verification_watcher_;
  // A finished future can still have its completion queued on the UI thread.
  bool verification_active_ = false;
  std::shared_ptr<std::atomic_bool> verification_canceled_;
  quint64 generation_ = 0;
  QByteArray output_;
  bool listening_ = false;
  bool ready_ = false;
  int health_failures_ = 0;
  quint16 requested_port_ = 0;
  std::unique_ptr<QLockFile> store_lock_;
};

}  // namespace photo_editor::desktop

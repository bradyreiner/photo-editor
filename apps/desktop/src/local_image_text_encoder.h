#pragma once

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>

namespace photo_editor::desktop {

// One owned stdio worker, one in-flight request, no implicit downloads/replays.
// Explicit paths currently support the isolated runtime; packaging is separate.
class LocalImageTextEncoder final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY changed)
 public:
  explicit LocalImageTextEncoder(QObject* parent = nullptr);
  ~LocalImageTextEncoder() override;
  bool start(const QString& python, const QString& worker, const QString& model_root, const QString& device);
  bool startExecutable(const QString& executable, const QString& model_root, const QString& device);
  QString encodeText(const QString& text);
  QString encodeImage(const QString& path);
  void cancel();
  void stop();
  QVariantMap snapshot() const;
 signals:
  void changed();
  void embeddingReady(const QString& id, const QVariantMap& result);
  void requestFailed(const QString& id, const QString& message);
  void requestCancelled(const QString& id);
 private:
  friend struct LocalImageTextEncoderTestAccess;
  bool startOwned(const QString& program, const QStringList& arguments, const QString& working_directory,
                  const QString& model_root, const QString& device);
  QString submit(const QString& kind, const QString& field, const QString& value);
  void readOutput();
  void fail(const QString& message);
  void terminateOwned();
  QPointer<QProcess> process_;
  QTimer deadline_;
  QByteArray output_, diagnostics_;
  QString state_ = QStringLiteral("stopped"), error_, device_, space_key_;
  QString pending_id_, pending_kind_;
  bool ready_ = false, discard_reply_ = false;
};
}  // namespace photo_editor::desktop
